#include "host_comm_internal.h"
#include "imu_calibration_protocol.h"
#include "thread_internal.h"
#include "SirinEmbedOS/Services/Services.h"
#include "Services/Logging/debug_port_internal.h"
#include "SirinEmbedOS/Services/Parameters/HostLink/ParameterProtocol.h"
#include "SirinEmbedOS/Diagnostics/Watch/Watch.h"
#include "cmsis_os2.h"
#include "stm32h7xx_hal.h"
static _Alignas(32) uint8_t tx_storage[SRNOS_HOST_LINK_TX_SLOT_COUNT * SRNOS_PROTOCOL_FRAME_SLOT_SIZE];
static _Alignas(32) uint8_t rx_drain[512];
static SRNOS_HostLinkSetupFn application_setup;
static void *application_user;
static bool prepared, storage_enabled;
static osThreadId_t comm_thread, command_thread;
SRNOS_DEBUG_WATCH_BSS volatile SRNOS_DbgHostLink SRNOS_DebugHostLink;
static uint32_t now_ms(void *user) { (void)user; return HAL_GetTick(); }
static uint32_t cycles(void *user) { (void)user; return DWT->CYCCNT; }
static bool transport_init(void *user) { (void)user; SRNOS_DebugPortInit(); return true; }
static bool transport_start(void *user, TaskHandle_t task, uint32_t bits)
{
    (void)user; SRNOS_DebugPortRegisterTxServiceTask(task);
    return SRNOS_DebugPortStartRx(task,bits) == SRNOS_DEBUG_PORT_RX_OK;
}
static size_t transport_read(void *user, uint8_t *data, size_t size) { (void)user; return SRNOS_DebugPortRead(data,size); }
static uint32_t transport_overflow(void *user) { (void)user; return SRNOS_DebugPortGetRxOverflowBytes(); }
static uint32_t transport_generation(void *user) { (void)user; return SRNOS_DebugPortGetSessionGeneration(); }
static bool transport_connected(void *user) { (void)user; return SRNOS_DebugPortIsConfigured(); }
static bool transport_complete(void *user, bool *success) { (void)user; return SRNOS_DebugPortTakeTxComplete(success); }
static SRNOS_HostLinkCoreTxResult transport_submit(void *user, const uint8_t *data, size_t size)
{
    (void)user;
    switch (SRNOS_DebugPortTryTransmit(data,size)) {
    case SRNOS_DEBUG_PORT_WRITE_OK: return SRNOS_HOST_LINK_TX_ACCEPTED;
    case SRNOS_DEBUG_PORT_WRITE_FULL: return SRNOS_HOST_LINK_TX_BUSY;
    case SRNOS_DEBUG_PORT_WRITE_NOT_CONFIGURED: return SRNOS_HOST_LINK_TX_DISCONNECTED;
    default: return SRNOS_HOST_LINK_TX_ERROR;
    }
}
static uint16_t storage_work(void *user, uint8_t service, uint8_t message,
 const uint8_t *request, size_t size, uint8_t *response, size_t capacity,
 size_t *length, uint8_t *flags)
{
    (void)user; (void)request; *flags=0;
    if (service != SRNOS_HOST_LINK_SERVICE_CONFIG || size != 0) { *length=0; return SRNOS_HOST_LINK_STATUS_BAD_LENGTH; }
    return SRNOS_ParameterProtocolRunStorage(message,response,capacity,length);
}
static bool defer_storage(void *user, uint8_t message, uint32_t sequence)
{
    (void)user;
    return SRNOS_HostLinkSubmitDeferred(SRNOS_HOST_LINK_SERVICE_CONFIG,message,sequence,
     NULL,0,storage_work,NULL) == SRNOS_HOST_LINK_OK;
}
static SRNOS_HostLinkResult setup_link(SRNOS_HostLinkRegistration *link, void *user)
{
    (void)user;
    const SRNOS_ParameterProtocolConfig params = { .defer=defer_storage, .user=NULL, .disable_storage=!storage_enabled };
    SRNOS_HostLinkResult result=SRNOS_ParameterProtocolRegister(link,&params);
    if (result == SRNOS_HOST_LINK_OK) result=SRNOS_IMUCalibrationProtocolRegister(link,storage_enabled);
    if (result == SRNOS_HOST_LINK_OK && application_setup) result=application_setup(link,application_user);
    return result;
}
static void snapshot(const SRNOS_HostLinkSnapshot *s, void *user)
{
    (void)user;
    SRNOS_DbgBegin(&SRNOS_DebugHostLink.h);
    SRNOS_DebugHostLink.started=s->started;
    SRNOS_DebugHostLink.usb_configured=s->link.transport_connected;
    SRNOS_DebugHostLink.rx_frames=s->link.rx_frames;
    SRNOS_DebugHostLink.rx_bytes=s->link.rx_bytes;
    SRNOS_DebugHostLink.tx_frames=s->link.tx_frames;
    SRNOS_DebugHostLink.tx_bytes=s->link.tx_bytes;
    SRNOS_DebugHostLink.cobs_errors=s->link.cobs_errors;
    SRNOS_DebugHostLink.crc_errors=s->link.crc_errors;
    SRNOS_DebugHostLink.length_errors=s->link.length_errors;
    SRNOS_DebugHostLink.version_errors=s->link.version_errors;
    SRNOS_DebugHostLink.flags_errors=s->link.flags_errors;
    SRNOS_DebugHostLink.kind_errors=s->link.kind_errors;
    SRNOS_DebugHostLink.unknown_service=s->link.unknown_service;
    SRNOS_DebugHostLink.unknown_message=s->link.unknown_message;
    SRNOS_DebugHostLink.rx_overflow=s->link.rx_overflow;
    SRNOS_DebugHostLink.sequence_gap=s->link.sequence_gap;
    SRNOS_DebugHostLink.sequence_duplicate=s->link.sequence_duplicate;
    SRNOS_DebugHostLink.sequence_out_of_order=s->link.sequence_out_of_order;
    SRNOS_DebugHostLink.request_replay=s->link.request_replay;
    SRNOS_DebugHostLink.request_conflict=s->link.request_conflict;
    SRNOS_DebugHostLink.transport_busy=s->link.transport_busy;
    SRNOS_DebugHostLink.transport_errors=s->link.transport_errors;
    SRNOS_DebugHostLink.transport_disconnects=s->link.transport_disconnects;
    SRNOS_DebugHostLink.processing_cycles_total=s->link.processing_cycles_total;
    SRNOS_DebugHostLink.processing_cycles_max=s->link.processing_cycles_max;
    SRNOS_DebugHostLink.transport_connected=s->link.transport_connected;
    for(unsigned i=0;i<SRNOS_HOST_LINK_QOS_COUNT;i++) {
        SRNOS_DebugHostLink.qos[i].rx_frames=s->link.qos[i].rx_frames;
        SRNOS_DebugHostLink.qos[i].rx_bytes=s->link.qos[i].rx_bytes;
        SRNOS_DebugHostLink.qos[i].tx_frames=s->link.qos[i].tx_frames;
        SRNOS_DebugHostLink.qos[i].tx_bytes=s->link.qos[i].tx_bytes;
        SRNOS_DebugHostLink.qos[i].submitted=s->link.qos[i].submitted;
        SRNOS_DebugHostLink.qos[i].sent=s->link.qos[i].sent;
        SRNOS_DebugHostLink.qos[i].rejected=s->link.qos[i].rejected;
        SRNOS_DebugHostLink.qos[i].drop_new=s->link.qos[i].drop_new;
        SRNOS_DebugHostLink.qos[i].drop_old=s->link.qos[i].drop_old;
        SRNOS_DebugHostLink.qos[i].coalesced=s->link.qos[i].coalesced;
        SRNOS_DebugHostLink.qos[i].queue_depth=s->link.qos[i].queue_depth;
        SRNOS_DebugHostLink.qos[i].queue_high_water=s->link.qos[i].queue_high_water;
        SRNOS_DebugHostLink.qos[i].last_wait_ms=s->link.qos[i].last_wait_ms;
        SRNOS_DebugHostLink.qos[i].max_wait_ms=s->link.qos[i].max_wait_ms;
    }

    SRNOS_DebugHostLink.comm_stack_high_water=s->comm_stack_high_water_words;
    SRNOS_DebugHostLink.worker_stack_high_water=s->worker_stack_high_water_words;
    SRNOS_DebugHostLink.deferred_depth=s->deferred_depth;
    SRNOS_DebugHostLink.deferred_high_water=s->deferred_high_water;
    SRNOS_DebugHostLink.source_registered=s->sources.registered_count;
    SRNOS_DebugHostLink.source_active=s->sources.active_count;
    SRNOS_DbgEnd(&SRNOS_DebugHostLink.h,HAL_GetTick(),0);
}
bool SRNOS_HostLinkPrepareEx(bool enable_usb, bool storage, SRNOS_HostLinkSetupFn setup, void *user)
{
    if (!enable_usb || prepared || osKernelGetState()==osKernelRunning) return false;
    storage_enabled=storage;
    application_setup=setup; application_user=user;
    SRNOS_DbgZero(&SRNOS_DebugHostLink,sizeof(SRNOS_DebugHostLink));
    SRNOS_DbgInit(&SRNOS_DebugHostLink.h,SRNOS_DBG_MODULE_HOST_LINK,0,sizeof(SRNOS_DebugHostLink));
    const SRNOS_HostLinkConfig config = {
      .transport={NULL,transport_init,transport_start,transport_read,transport_overflow,
                  transport_submit,transport_complete,transport_connected,transport_generation},
      .now_ms=now_ms,.cycles=cycles,.clock_user=NULL,
      .tx_storage=tx_storage,.tx_storage_size=sizeof(tx_storage),
      .rx_drain_buffer=rx_drain,.rx_drain_buffer_size=sizeof(rx_drain),
      .project_setup=setup_link,.snapshot=snapshot,.project_user=NULL,
      .task_wait_ms=1,.snapshot_period_ms=100,
    };
    if (SRNOS_HostLinkConfigure(&config)!=SRNOS_HOST_LINK_OK ||
        SRNOS_HostLinkPrepareLink()!=SRNOS_HOST_LINK_OK) return false;
    prepared=true; return true;
}
bool SRNOS_HostLinkStart(void)
{
    if (!prepared || comm_thread || command_thread) return false;
    if (!SRNOS_HostLinkInitResources()) return false;
    const osThreadAttr_t command={.name="SRNOS_HostCmd",.stack_size=2048,.priority=osPriorityLow1};
    /* RX codec -> application handler -> deferred queue includes FP context.
     * Keep SRNOS_SystemHeapBudget's HostLink reserve in sync with this stack. */
    const osThreadAttr_t comm={.name="SRNOS_HostComm",.stack_size=3072,.priority=osPriorityNormal};
    command_thread=SRNOS_KernelThreadNew(SRNOS_HostLinkWorkerTask,NULL,&command);
    if (!command_thread) return false;
    comm_thread=SRNOS_KernelThreadNew(SRNOS_HostLinkTask,NULL,&comm);
    if (!comm_thread) { (void)SRNOS_KernelThreadAbortStartup(command_thread); command_thread=NULL; return false; }
    return true;
}

bool SRNOS_HostLinkPrepare(bool usb,SRNOS_HostLinkSetupFn setup,void *user)
{ return SRNOS_HostLinkPrepareEx(usb,true,setup,user); }
