#include "kernel_internal.h"
#include "startup_barrier.h"
#include "SirinEmbedOS/Kernel/Startup.h"
#include "SirinEmbedOS/Kernel/System.h"
#include "SirinEmbedOS/Services/Services.h"
#include "SirinEmbedOS/Services/Referee/Startup.h"
#include "SirinEmbedOS/Diagnostics/Runtime/Startup.h"
#include "SirinEmbedOS/IO/SPI/bsp_spi.h"
#include "SirinEmbedOS/IO/I2C/bsp_i2c.h"
#include "SirinEmbedOS/IO/CAN/Native/bsp_fdcan.h"
#include "SirinEmbedOS/IO/CAN/Router/NTFDCAN_Router.h"
#include "SirinEmbedOS/IO/CAN/External/EXFDCAN_Router.h"
#include "SirinEmbedOS/IO/RS485/RS485_Router.h"
#include "SirinEmbedOS/Devices/IMU/Worker/tskptt_imu.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"

extern SRNOS_SystemResult SRNOS_SystemFreeze(const SRNOS_SystemConfig *);
extern bool SRNOS_BoardIndicatorsPrepare(void);
extern void SRNOS_BoardIndicatorsWorker(void *);
extern void SRNOS_CAN_PrepareResources(void);
static SRNOS_SystemStatus status;
static osEventFlagsId_t application_ready;
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
    configASSERT(SRNOS_BoardIndicatorsPrepare());
    status.native_can_ready = bsp_fdcan_init_all() && bsp_fdcan_start_all();
    configASSERT((osEventFlagsSet(application_ready, 2u) & osFlagsError) == 0u);
    status.parameter_load_status = c->load_parameters ? (int)params_load() : (int)PARAMS_STATUS_OK;
    if (c->usb_enabled) status.usb_ready = SRNOS_USBStart();
    status.phase = SRNOS_SYSTEM_RUNNING;
    configASSERT((osEventFlagsSet(application_ready, 1u) & osFlagsError) == 0u);
    SRNOS_ApplicationMain();
    osThreadExit();
}
static void configured_application_thread(void *arg)
{
    const SRNOS_ThreadConfig *thread = arg;
    configASSERT(SRNOS_StartupWaitReady(application_ready, 1u));
    thread->entry(thread->argument);
    osThreadExit();
}
static void make_thread(const char *name, osThreadFunc_t entry, void *arg, uint32_t stack, osPriority_t priority)
{
    const osThreadAttr_t attributes = { .name=name, .stack_size=stack, .priority=priority };
    configASSERT(osThreadNew(entry, arg, &attributes) != NULL);
}
typedef struct {
    const char *name;
    osThreadFunc_t entry;
    void *arg;
    uint32_t stack;
    osPriority_t priority;
    bool wait_board;
} system_thread_t;
static const system_thread_t system_threads[] = {
    {"CAN1_RX",Start_CAN_RxRouter,(void *)1,4096,osPriorityHigh,true},
    {"CAN1_TX",Start_CAN_TxRouter,(void *)1,4096,osPriorityHigh,true},
    {"CAN2_RX",Start_CAN_RxRouter,(void *)2,4096,osPriorityHigh,true},
    {"CAN2_TX",Start_CAN_TxRouter,(void *)2,4096,osPriorityHigh,true},
    {"CAN3_RX",Start_CAN_RxRouter,(void *)3,4096,osPriorityHigh,true},
    {"CAN3_TX",Start_CAN_TxRouter,(void *)3,4096,osPriorityHigh,true},
    {"CAN4_RX",Start_EXFDCAN_RxRouter,NULL,4096,osPriorityHigh4,false},
    {"CAN4_TX",Start_EXFDCAN_TxRouter,NULL,4096,osPriorityHigh4,false},
    {"IMU",TSKPTT_IMU_Task,NULL,2048,osPriorityHigh5,false},
    {"Indicators",SRNOS_BoardIndicatorsWorker,NULL,1024,osPriorityLow,true},
};
static void system_thread_entry(void *arg)
{
    const system_thread_t *t=arg;
    if (t->wait_board) configASSERT(SRNOS_StartupWaitReady(application_ready, 2u));
    t->entry(t->arg);
    /* Failed device initialization is reported by the device status. FreeRTOS
     * task entries must never return into the port's fatal return trap. */
    osThreadExit();
}
void SRNOS_KernelStart(void)
{
    static unsigned started;
    configASSERT(started == 0U);
    started = 1U;
    SRNOS_SystemConfig draft;
    SRNOS_SystemConfigDefaults(&draft);
    bsp_spi_init_pre();
    bsp_i2c_init();
    SRNOS_ApplicationConfigure(&draft);
    configASSERT(SRNOS_SystemValidate(&draft) == SRNOS_SYSTEM_OK);
    configASSERT(SRNOS_SerialConfigure(draft.serial, draft.serial_count) == SRNOS_SERIAL_OK);
    configASSERT(SRNOS_RemoteControlConfigure(&draft.remote) == 0);
    configASSERT(SRNOS_RefereeConfigure(draft.referee_port) == 0);
    if (draft.logging_enabled) {
        configASSERT(SRNOS_LoggingPrepare(draft.log_backends, SRNOS_SerialGetChannel(draft.log_port)));
    }
    if (draft.host_link_enabled) {
        configASSERT(SRNOS_HostLinkPrepare(draft.usb_enabled, draft.host_link_setup, draft.host_link_user));
    }
    if (draft.bind_channels) configASSERT(draft.bind_channels(draft.bind_channels_user));
    configASSERT(SRNOS_IMU_Prepare());
    configASSERT(SRNOS_DiagnosticsPrepare(draft.diagnostics_enabled));
    configASSERT(SRNOS_ParametersFreeze() == PARAMS_STATUS_OK);
    SRNOS_CAN_FreezeConfiguration();
    configASSERT(SRNOS_SystemFreeze(&draft) == SRNOS_SYSTEM_OK);
    status.phase = SRNOS_SYSTEM_CONFIG_FROZEN;
    const SRNOS_SystemConfig *c = SRNOS_SystemGetConfig();

    configASSERT(osKernelInitialize() == osOK);
    application_ready = osEventFlagsNew(NULL);
    configASSERT(application_ready != NULL);
    bsp_spi_init_rtos();
    bsp_i2c_init_rtos();
    SRNOS_CAN_PrepareResources();
    configASSERT(SRNOS_SerialStart() == SRNOS_SERIAL_OK);
    configASSERT(SRNOS_RemoteControlStart() == 0);
    configASSERT(SRNOS_RefereeStart() == 0);
    configASSERT(SRNOS_RS485Start(c->rs485_response_timeout_ms) == 0);
    configASSERT(SRNOS_DiagnosticsStart());
    if (c->logging_enabled) configASSERT(SRNOS_LoggingStart());
    if (c->host_link_enabled) configASSERT(SRNOS_HostLinkStart());
    /* Preserve the prototype's bounded shared-SPI worker priority ordering:
     * SPI callback High6 > IMU High5 > external CAN High4 > native CAN High. */
    for (size_t i=0; i<sizeof(system_threads)/sizeof(system_threads[0]); ++i) {
        const system_thread_t *t=&system_threads[i];
        make_thread(t->name, system_thread_entry, (void *)t, t->stack, t->priority);
    }
    for (size_t i=0; i<c->thread_count; ++i) {
        const SRNOS_ThreadConfig *t = &c->threads[i];
        make_thread(t->name, configured_application_thread, (void *)t, t->stack_bytes, (osPriority_t)t->priority);
    }
    make_thread("Application", application_thread, NULL, c->application_stack_bytes, (osPriority_t)c->application_priority);
    status.phase = SRNOS_SYSTEM_THREADS_CREATED;
    (void)osKernelStart();
    SRNOS_KernelFailure("scheduler returned", __FILE__, __LINE__);
}
