#include "IO/Ethernet/ethernet_internal.h"
#include "Services/Network/network_internal.h"
#include "thread_internal.h"
#include "Services/Input/input_internal.h"
#include "Services/Message/message_internal.h"
#include "Services/Blackbox/blackbox_internal.h"
#include "kernel_internal.h"
#include "startup_barrier.h"
#include "SirinEmbedOS/Kernel/Startup.h"
#include "SirinEmbedOS/Kernel/System.h"
#include "SirinEmbedOS/Services/Services.h"
#include "SirinEmbedOS/Services/Logging/Log.h"
#include "SirinEmbedOS/Services/Referee/Startup.h"
#include "../Services/SystemMonitor/monitor_internal.h"
#include "SirinEmbedOS/Services/SirinLink/SystemMonitorTelemetry.h"
#include "IO/SPI/spi_internal.h"
#include "IO/I2C/i2c_internal.h"
#include "IO/CAN/Native/fdcan_internal.h"
#include "IO/CAN/can_startup.h"
#include "IO/CAN/can_controller_internal.h"
#include "IO/RS485/rs485_internal.h"
#include "../Services/INS/ins_internal.h"
#include "../Services/INS/imu_calibration_internal.h"
#include "Services/Parameters/parameters_internal.h"
#include "Services/Telemetry/telemetry_internal.h"
#include "../Services/Indicators/indicators_internal.h"
#include "SirinEmbedOS/Services/SirinLink/IMUTelemetry.h"
#include "SirinEmbedOS/Services/SirinLink/INSTelemetry.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"

extern void SRNOS_SensorsPrepare(const SRNOS_SensorConfig *);
extern void SRNOS_BMIWorker(void *);
extern void SRNOS_LSMWorker(void *);
extern void SRNOS_EnvironmentWorker(void *);
extern SRNOS_SystemResult SRNOS_SystemFreeze(const SRNOS_SystemConfig *);
static SRNOS_IMUSampleObserver app_imu_observer;
static void *app_imu_user;
static void observe_imu(SRNOS_SensorId id,const SRNOS_IMUSample *sample,void *user) {
    (void)user;SRNOS_IMUTelemetryObserve(id,sample,NULL);
    if(app_imu_observer&&app_imu_observer!=SRNOS_IMUTelemetryObserve) app_imu_observer(id,sample,app_imu_user);
}
static bool ins_link_enabled, monitor_link_enabled;
static SRNOS_SirinLinkSetupFn app_host_setup;
static void *app_host_user;
static SRNOS_SirinLinkResult setup_host(SRNOS_SirinLinkRegistration *link,void *user) {
    (void)user;SRNOS_SirinLinkResult r=SRNOS_SIRIN_LINK_OK;
    if(ins_link_enabled) {
        r=SRNOS_IMUTelemetryRegister(link,NULL);
        if(r==SRNOS_SIRIN_LINK_OK)r=SRNOS_INSTelemetryRegister(link,NULL);
    }
    if(r==SRNOS_SIRIN_LINK_OK&&monitor_link_enabled) r=SRNOS_SystemMonitorTelemetryRegister(link,NULL);
    if(r==SRNOS_SIRIN_LINK_OK)r=SRNOS_MessageRegisterHost(link);
    if(r==SRNOS_SIRIN_LINK_OK&&app_host_setup && (!ins_link_enabled ||
        (app_host_setup!=SRNOS_IMUTelemetryRegister&&app_host_setup!=SRNOS_INSTelemetryRegister)))
        r=app_host_setup(link,app_host_user);
    return r;
}
static SRNOS_SystemStatus status;
static osEventFlagsId_t application_ready;
static uint32_t startup_remaining(uint32_t deadline)
{
    int32_t left=(int32_t)(deadline-SRNOS_TimeMs());
    return left>0?(uint32_t)left:0;
}
__attribute__((weak)) void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config) { (void)config; }

void SRNOS_SystemGetStatus(SRNOS_SystemStatus *out)
{
    if (!out) return;
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    *out = status;
    __set_PRIMASK(mask);
}
static void application_thread(void *argument)
{
    (void)argument;
    const SRNOS_SystemConfig *c = SRNOS_SystemGetConfig();
    status.native_can_ready = SRNOS_FDCANInitAll() && SRNOS_FDCANStartAll();
    configASSERT((osEventFlagsSet(application_ready, 2u) & osFlagsError) == 0u);
    const uint32_t deadline=SRNOS_IMUCalibrationStartupBegin(SRNOS_TimeMs());
    uint32_t left=startup_remaining(deadline);
    status.identity_load_status=left?SRNOS_StorageLoadIdentity(left):SRNOS_STORAGE_TIMEOUT;
    SRNOS_IMUCalibrationLoadStartup(deadline);
    status.parameter_load_status = c->load_parameters ?
        (int)SRNOS_ParametersLoadStartup(startup_remaining(deadline)) : (int)SRNOS_PARAMETERS_STATUS_OK;
    if (c->usb_enabled) status.usb_ready = SRNOS_USBStart();
    status.phase = SRNOS_SYSTEM_RUNNING;
    SRNOS_KernelThreadReleaseApplications();
    configASSERT((osEventFlagsSet(application_ready, 1u) & osFlagsError) == 0u);
    SRNOS_ApplicationMain();
    return;
}
static void make_thread(const char *name, osThreadFunc_t entry, void *arg, uint32_t stack, osPriority_t priority)
{
    const osThreadAttr_t attributes = { .name=name, .stack_size=stack, .priority=priority };
    configASSERT(SRNOS_KernelThreadNew(entry, arg, &attributes) != NULL);
}
typedef struct {
    const char *name;
    osThreadFunc_t entry;
    void *arg;
    uint32_t stack;
    osPriority_t priority;
    bool wait_board;
} system_thread_t;
/* Fixed CAN/BMI/bus/indicator workers, native idle/timer and shared startup
 * overhead. Keep this conservative allowance until board measurements justify
 * a separate change; optional service reservations are owned by each service. */
#define SRNOS_KERNEL_FIXED_HEAP_RESERVE_BYTES (60u * 1024u)
#define SRNOS_KERNEL_THREAD_OVERHEAD_BYTES 512u
#define SRNOS_KERNEL_INS_STACK_BYTES 3072u
#define SRNOS_KERNEL_SENSOR_STACK_BYTES 2048u
#define SRNOS_KERNEL_BUS_COMPLETION_RESERVE_BYTES 2560u

uint64_t SRNOS_KernelStartupHeapBudget(const SRNOS_SystemConfig *config)
{
    uint64_t bytes = (uint64_t)SRNOS_KERNEL_FIXED_HEAP_RESERVE_BYTES + config->application_stack_bytes +
        SRNOS_KERNEL_THREAD_OVERHEAD_BYTES + SRNOS_KERNEL_INS_STACK_BYTES +
        SRNOS_KERNEL_THREAD_OVERHEAD_BYTES + SRNOS_KERNEL_BUS_COMPLETION_RESERVE_BYTES;
    if (config->sensors.lsm_enabled) bytes += SRNOS_KERNEL_SENSOR_STACK_BYTES + SRNOS_KERNEL_THREAD_OVERHEAD_BYTES;
    if (config->sensors.lis_enabled || config->sensors.spa_enabled)
        bytes += SRNOS_KERNEL_SENSOR_STACK_BYTES + SRNOS_KERNEL_THREAD_OVERHEAD_BYTES;
    return bytes;
}
static const system_thread_t system_threads[] = {
    {"SRNOS_CAN1_RX",SRNOS_NativeCANRxRouterWorker,(void *)1,4096,osPriorityRealtime,true},
    {"SRNOS_CAN1_TX",SRNOS_NativeCANTxRouterWorker,(void *)1,4096,osPriorityRealtime,true},
    {"SRNOS_CAN2_RX",SRNOS_NativeCANRxRouterWorker,(void *)2,4096,osPriorityRealtime,true},
    {"SRNOS_CAN2_TX",SRNOS_NativeCANTxRouterWorker,(void *)2,4096,osPriorityRealtime,true},
    {"SRNOS_CAN3_RX",SRNOS_NativeCANRxRouterWorker,(void *)3,4096,osPriorityRealtime,true},
    {"SRNOS_CAN3_TX",SRNOS_NativeCANTxRouterWorker,(void *)3,4096,osPriorityRealtime,true},
    {"SRNOS_CAN4",SRNOS_ExternalCANWorker,NULL,4096,osPriorityHigh7,false},
    {"SRNOS_BMI",SRNOS_BMIWorker,NULL,2048,osPriorityRealtime,false},
    {"SRNOS_INS",SRNOS_INSWorker,NULL,SRNOS_KERNEL_INS_STACK_BYTES,osPriorityRealtime,false},
    {"SRNOS_Indic",SRNOS_IndicatorsWorker,NULL,1024,osPriorityLow,false},
};
static void system_thread_entry(void *arg)
{
    const system_thread_t *t=arg;
    if (t->wait_board) configASSERT(SRNOS_StartupWaitReady(application_ready, 2u));
    t->entry(t->arg);
    /* Failed device initialization is reported by the device status. FreeRTOS
     * task entries must never return into the port's fatal return trap. */
    return;
}
void SRNOS_KernelStart(void)
{
    static unsigned started;
    configASSERT(started == 0U);
    started = 1U;
    configASSERT(SRNOS_BoardBootIdentityInitialize());
    SRNOS_SystemConfig draft;
    SRNOS_SystemConfigDefaults(&draft);
    SRNOS_SPIInitPre();
    SRNOS_I2CInit();
    SRNOS_ApplicationConfigure(&draft);
    configASSERT(SRNOS_SystemValidate(&draft) == SRNOS_SYSTEM_OK);
    configASSERT(SRNOS_SerialConfigure(draft.serial, draft.serial_count) == SRNOS_SERIAL_OK);
    configASSERT(SRNOS_MessageTransportPrepare(&draft.messages));
    configASSERT(SRNOS_RemoteControlConfigure(&draft.remote) == 0);
    configASSERT(SRNOS_RefereeConfigure(draft.referee_port) == 0);
    if (draft.logging_enabled) {
        SRNOS_SerialChannel *log_channel = (draft.log_backends & SRNOS_LOG_BACKEND_UART) ?
            SRNOS_SerialGetChannel(draft.log_port) : NULL;
        configASSERT(SRNOS_LoggingPrepare(draft.log_backends, log_channel));
    }
    if (draft.bind_channels) configASSERT(draft.bind_channels(draft.bind_channels_user));
    configASSERT(SRNOS_InputFreeze());
    configASSERT(SRNOS_MessageFreeze());
    monitor_link_enabled=draft.system_monitor_enabled;
    ins_link_enabled=draft.ins.enabled;app_host_setup=draft.sirin_link_setup;app_host_user=draft.sirin_link_user;
    if (draft.sirin_link_enabled) {
        configASSERT(SRNOS_SirinLinkPrepareEx(draft.usb_enabled, draft.sirin_link_storage_enabled, setup_host, NULL));
    }
    app_imu_observer=draft.sensors.imu_observer;app_imu_user=draft.sensors.imu_observer_user;
    draft.sensors.imu_observer=observe_imu;draft.sensors.imu_observer_user=NULL;
    configASSERT(SRNOS_EthernetPrepare(&draft.ethernet));
    configASSERT(SRNOS_NetworkPrepare(&draft.network));
    SRNOS_SensorsPrepare(&draft.sensors);
    configASSERT(SRNOS_SystemMonitorPrepare(draft.system_monitor_enabled));
    configASSERT(SRNOS_StoragePrepare(&draft.storage));
    configASSERT(SRNOS_BlackboxPrepare(&draft.blackbox));
    SRNOS_IMUCalibrationPrepare(draft.storage.enabled);
    SRNOS_ParametersConfigure(draft.parameter_application_id,draft.parameter_compatibility_version);
    configASSERT(SRNOS_TelemetryFreeze());
    configASSERT(SRNOS_ParametersFreeze() == SRNOS_PARAMETERS_STATUS_OK);
    configASSERT(SRNOS_CANControllerPrepare(draft.can));
    SRNOS_CAN_FreezeConfiguration();
    configASSERT(SRNOS_SystemFreeze(&draft) == SRNOS_SYSTEM_OK);
    status.phase = SRNOS_SYSTEM_CONFIG_FROZEN;
    const SRNOS_SystemConfig *c = SRNOS_SystemGetConfig();
    SRNOS_INSPrepare(&c->ins);

    configASSERT(SRNOS_IndicatorsPrepare(c->bringup_indicators));
    configASSERT(osKernelInitialize() == osOK);
    configASSERT(SRNOS_InputStart());
    configASSERT(SRNOS_MessageStart());
    configASSERT(SRNOS_MessageTransportStart());
    configASSERT(SRNOS_KernelThreadPrepare(c->cycle_statistics_enabled));
    application_ready = osEventFlagsNew(NULL);
    configASSERT(application_ready != NULL);
    SRNOS_SPIInitRtos();
    SRNOS_I2CInitRtos();
    SRNOS_CAN_PrepareResources();
    configASSERT(SRNOS_SerialStart() == SRNOS_SERIAL_OK);
    configASSERT(SRNOS_RemoteControlStart() == 0);
    configASSERT(SRNOS_RefereeStart() == 0);
    configASSERT(SRNOS_RS485Start(c->SRNOS_RS485ResponseTimeoutMs) == 0);
    configASSERT(SRNOS_SystemMonitorStart());
    configASSERT(SRNOS_EthernetStart());
    configASSERT(SRNOS_NetworkStart());
    configASSERT(SRNOS_BlackboxStart());
    if (c->logging_enabled) configASSERT(SRNOS_LoggingStart());
    if (c->sirin_link_enabled) configASSERT(SRNOS_SirinLinkStart());
    /* P0: native CAN and IMU/INS Realtime (48). P1: CAN4 and UART High7
     * (47). Shared SPI stays Realtime1 (49) to complete either class, with
     * IMU-first bounded arbitration. I2C remains High6 (46). */
    for (size_t i=0; i<sizeof(system_threads)/sizeof(system_threads[0]); ++i) {
        const system_thread_t *t=&system_threads[i];
        make_thread(t->name, system_thread_entry, (void *)t, t->stack, t->priority);
    }
    if(c->sensors.lsm_enabled) make_thread("SRNOS_LSM", SRNOS_LSMWorker, NULL, SRNOS_KERNEL_SENSOR_STACK_BYTES, osPriorityRealtime);
    if(c->sensors.lis_enabled || c->sensors.spa_enabled) make_thread("SRNOS_Env", SRNOS_EnvironmentWorker, NULL, SRNOS_KERNEL_SENSOR_STACK_BYTES, osPriorityNormal);
    for (size_t i=0; i<c->thread_count; ++i) {
        const SRNOS_ThreadConfig *t = &c->threads[i];
        SRNOS_Thread handle;
        configASSERT(SRNOS_KernelThreadCreateConfigured(t,&handle)==SRNOS_OK);
    }
    make_thread("SRNOS_AppInit", application_thread, NULL, c->application_stack_bytes, (osPriority_t)c->application_priority);
    status.phase = SRNOS_SYSTEM_THREADS_CREATED;
    (void)osKernelStart();
    SRNOS_KernelFailure("scheduler returned", __FILE__, __LINE__);
}
