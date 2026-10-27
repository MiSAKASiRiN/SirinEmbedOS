#include "host_comm_internal.h"
#include "imu_calibration_protocol.h"
#include "thread_internal.h"
#include "SirinEmbedOS/Services/Services.h"
#include "Services/Logging/debug_port_internal.h"
#include "SirinEmbedOS/Services/Parameters/SirinLink/ParameterProtocol.h"
#include "SirinEmbedOS/Diagnostics/Watch/Watch.h"
#include "cmsis_os2.h"
#include "stm32h7xx_hal.h"
static _Alignas(32) uint8_t tx_storage[SRNOS_SIRIN_LINK_TX_SLOT_COUNT * SRNOS_PROTOCOL_FRAME_SLOT_SIZE];
static _Alignas(32) uint8_t rx_drain[512];
static SRNOS_SirinLinkSetupFn application_setup;
static void *application_user;
static bool prepared, storage_enabled;
static osThreadId_t comm_thread, command_thread;
SRNOS_DEBUG_WATCH_BSS volatile SRNOS_DbgSirinLink SRNOS_DebugSirinLink;
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
static SRNOS_SirinLinkCoreTxResult transport_submit(void *user, const uint8_t *data, size_t size)
{
    (void)user;
    switch (SRNOS_DebugPortTryTransmit(data,size)) {
    case SRNOS_DEBUG_PORT_WRITE_OK: return SRNOS_SIRIN_LINK_TX_ACCEPTED;
    case SRNOS_DEBUG_PORT_WRITE_FULL: return SRNOS_SIRIN_LINK_TX_BUSY;
    case SRNOS_DEBUG_PORT_WRITE_NOT_CONFIGURED: return SRNOS_SIRIN_LINK_TX_DISCONNECTED;
    default: return SRNOS_SIRIN_LINK_TX_ERROR;
    }
}
static uint16_t storage_work(void *user, uint8_t service, uint8_t message,
 const uint8_t *request, size_t size, uint8_t *response, size_t capacity,
 size_t *length, uint8_t *flags)
{
    (void)user; (void)request; *flags=0;
    if (service != SRNOS_SIRIN_LINK_SERVICE_CONFIG || size != 0) { *length=0; return SRNOS_SIRIN_LINK_STATUS_BAD_LENGTH; }
    return SRNOS_ParameterProtocolRunStorage(message,response,capacity,length);
}
static bool defer_storage(void *user, uint8_t message, uint32_t sequence)
{
    (void)user;
    return SRNOS_SirinLinkSubmitDeferred(SRNOS_SIRIN_LINK_SERVICE_CONFIG,message,sequence,
     NULL,0,storage_work,NULL) == SRNOS_SIRIN_LINK_OK;
}
static SRNOS_SirinLinkResult setup_link(SRNOS_SirinLinkRegistration *link, void *user)
{
    (void)user;
    const SRNOS_ParameterProtocolConfig params = { .defer=defer_storage, .user=NULL, .disable_storage=!storage_enabled };
    SRNOS_SirinLinkResult result=SRNOS_ParameterProtocolRegister(link,&params);
    if (result == SRNOS_SIRIN_LINK_OK) result=SRNOS_IMUCalibrationProtocolRegister(link,storage_enabled);
    if (result == SRNOS_SIRIN_LINK_OK && application_setup) result=application_setup(link,application_user);
    return result;
}
static void snapshot(const SRNOS_SirinLinkSnapshot *s, void *user)
{
    (void)user;
    SRNOS_DbgBegin(&SRNOS_DebugSirinLink.h);
    SRNOS_DebugSirinLink.started=s->started;
    SRNOS_DebugSirinLink.usb_configured=s->link.transport_connected;
    SRNOS_DebugSirinLink.rx_frames=s->link.rx_frames;
    SRNOS_DebugSirinLink.rx_bytes=s->link.rx_bytes;
    SRNOS_DebugSirinLink.tx_frames=s->link.tx_frames;
    SRNOS_DebugSirinLink.tx_bytes=s->link.tx_bytes;
    SRNOS_DebugSirinLink.cobs_errors=s->link.cobs_errors;
    SRNOS_DebugSirinLink.crc_errors=s->link.crc_errors;
    SRNOS_DebugSirinLink.length_errors=s->link.length_errors;
    SRNOS_DebugSirinLink.version_errors=s->link.version_errors;
    SRNOS_DebugSirinLink.flags_errors=s->link.flags_errors;
    SRNOS_DebugSirinLink.kind_errors=s->link.kind_errors;
    SRNOS_DebugSirinLink.unknown_service=s->link.unknown_service;
    SRNOS_DebugSirinLink.unknown_message=s->link.unknown_message;
    SRNOS_DebugSirinLink.rx_overflow=s->link.rx_overflow;
    SRNOS_DebugSirinLink.sequence_gap=s->link.sequence_gap;
    SRNOS_DebugSirinLink.sequence_duplicate=s->link.sequence_duplicate;
    SRNOS_DebugSirinLink.sequence_out_of_order=s->link.sequence_out_of_order;
    SRNOS_DebugSirinLink.request_replay=s->link.request_replay;
    SRNOS_DebugSirinLink.request_conflict=s->link.request_conflict;
    SRNOS_DebugSirinLink.transport_busy=s->link.transport_busy;
    SRNOS_DebugSirinLink.transport_errors=s->link.transport_errors;
    SRNOS_DebugSirinLink.transport_disconnects=s->link.transport_disconnects;
    SRNOS_DebugSirinLink.processing_cycles_total=s->link.processing_cycles_total;
    SRNOS_DebugSirinLink.processing_cycles_max=s->link.processing_cycles_max;
    SRNOS_DebugSirinLink.transport_connected=s->link.transport_connected;
    for(unsigned i=0;i<SRNOS_SIRIN_LINK_QOS_COUNT;i++) {
        SRNOS_DebugSirinLink.qos[i].rx_frames=s->link.qos[i].rx_frames;
        SRNOS_DebugSirinLink.qos[i].rx_bytes=s->link.qos[i].rx_bytes;
        SRNOS_DebugSirinLink.qos[i].tx_frames=s->link.qos[i].tx_frames;
        SRNOS_DebugSirinLink.qos[i].tx_bytes=s->link.qos[i].tx_bytes;
        SRNOS_DebugSirinLink.qos[i].submitted=s->link.qos[i].submitted;
        SRNOS_DebugSirinLink.qos[i].sent=s->link.qos[i].sent;
        SRNOS_DebugSirinLink.qos[i].rejected=s->link.qos[i].rejected;
        SRNOS_DebugSirinLink.qos[i].drop_new=s->link.qos[i].drop_new;
        SRNOS_DebugSirinLink.qos[i].drop_old=s->link.qos[i].drop_old;
        SRNOS_DebugSirinLink.qos[i].coalesced=s->link.qos[i].coalesced;
        SRNOS_DebugSirinLink.qos[i].queue_depth=s->link.qos[i].queue_depth;
        SRNOS_DebugSirinLink.qos[i].queue_high_water=s->link.qos[i].queue_high_water;
        SRNOS_DebugSirinLink.qos[i].last_wait_ms=s->link.qos[i].last_wait_ms;
        SRNOS_DebugSirinLink.qos[i].max_wait_ms=s->link.qos[i].max_wait_ms;
    }

    SRNOS_DebugSirinLink.comm_stack_high_water=s->comm_stack_high_water_words;
    SRNOS_DebugSirinLink.worker_stack_high_water=s->worker_stack_high_water_words;
    SRNOS_DebugSirinLink.deferred_depth=s->deferred_depth;
    SRNOS_DebugSirinLink.deferred_high_water=s->deferred_high_water;
    SRNOS_DebugSirinLink.source_registered=s->sources.registered_count;
    SRNOS_DebugSirinLink.source_active=s->sources.active_count;
    SRNOS_DbgEnd(&SRNOS_DebugSirinLink.h,HAL_GetTick(),0);
}
bool SRNOS_SirinLinkPrepareEx(bool enable_usb, bool storage, SRNOS_SirinLinkSetupFn setup, void *user)
{
    if (!enable_usb || prepared || osKernelGetState()==osKernelRunning) return false;
    storage_enabled=storage;
    application_setup=setup; application_user=user;
    SRNOS_DbgZero(&SRNOS_DebugSirinLink,sizeof(SRNOS_DebugSirinLink));
    SRNOS_DbgInit(&SRNOS_DebugSirinLink.h,SRNOS_DBG_MODULE_SIRIN_LINK,0,sizeof(SRNOS_DebugSirinLink));
    const SRNOS_SirinLinkConfig config = {
      .transport={NULL,transport_init,transport_start,transport_read,transport_overflow,
                  transport_submit,transport_complete,transport_connected,transport_generation},
      .now_ms=now_ms,.cycles=cycles,.clock_user=NULL,
      .tx_storage=tx_storage,.tx_storage_size=sizeof(tx_storage),
      .rx_drain_buffer=rx_drain,.rx_drain_buffer_size=sizeof(rx_drain),
      .project_setup=setup_link,.snapshot=snapshot,.project_user=NULL,
      .task_wait_ms=1,.snapshot_period_ms=100,
    };
    if (SRNOS_SirinLinkConfigure(&config)!=SRNOS_SIRIN_LINK_OK ||
        SRNOS_SirinLinkPrepareLink()!=SRNOS_SIRIN_LINK_OK) return false;
    prepared=true; return true;
}
bool SRNOS_SirinLinkStart(void)
{
    if (!prepared || comm_thread || command_thread) return false;
    if (!SRNOS_SirinLinkInitResources()) return false;
    const osThreadAttr_t command={.name="SRNOS_HostCmd",.stack_size=2048,.priority=osPriorityLow1};
    /* RX codec -> application handler -> deferred queue includes FP context.
     * Keep SRNOS_SystemHeapBudget's SirinLink reserve in sync with this stack. */
    const osThreadAttr_t comm={.name="SRNOS_HostComm",.stack_size=3072,.priority=osPriorityNormal};
    command_thread=SRNOS_KernelThreadNew(SRNOS_SirinLinkWorkerTask,NULL,&command);
    if (!command_thread) return false;
    comm_thread=SRNOS_KernelThreadNew(SRNOS_SirinLinkTask,NULL,&comm);
    if (!comm_thread) { (void)SRNOS_KernelThreadAbortStartup(command_thread); command_thread=NULL; return false; }
    return true;
}

bool SRNOS_SirinLinkPrepare(bool usb,SRNOS_SirinLinkSetupFn setup,void *user)
{ return SRNOS_SirinLinkPrepareEx(usb,true,setup,user); }
