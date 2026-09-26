#include "SirinEmbedOS/Services/Services.h"
#include "SirinEmbedOS/Services/Logging/bsp_srn_debug_port.h"
#include "SirinEmbedOS/Services/Parameters/HostLink/tskptt_param_console.h"
#include "SirinEmbedOS/Diagnostics/Watch/omx_debug_watch.h"
#include "cmsis_os2.h"
#include "stm32h7xx_hal.h"
static _Alignas(32) uint8_t tx_storage[SRN_LINK_TX_SLOT_COUNT * SRN_COBS_PROT_FRAME_SLOT_SIZE];
static _Alignas(32) uint8_t rx_drain[512];
static SRNOS_HostLinkSetupFn application_setup;
static void *application_user;
static bool prepared;
static osThreadId_t comm_thread, command_thread;
OMX_DEBUG_WATCH_BSS volatile omx_dbg_srn_link_t g_omx_dbg_srn_link;
static uint32_t now_ms(void *user) { (void)user; return HAL_GetTick(); }
static uint32_t cycles(void *user) { (void)user; return DWT->CYCCNT; }
static bool transport_init(void *user) { (void)user; SRN_DebugPort_Init(); return true; }
static bool transport_start(void *user, TaskHandle_t task, uint32_t bits)
{
    (void)user; SRN_DebugPort_RegisterTxServiceTask(task);
    return SRN_DebugPort_StartRx(task,bits) == SRN_DEBUG_PORT_RX_OK;
}
static size_t transport_read(void *user, uint8_t *data, size_t size) { (void)user; return SRN_DebugPort_Read(data,size); }
static uint32_t transport_overflow(void *user) { (void)user; return SRN_DebugPort_GetRxOverflowBytes(); }
static bool transport_connected(void *user) { (void)user; return SRN_DebugPort_IsConfigured(); }
static bool transport_complete(void *user, bool *success) { (void)user; return SRN_DebugPort_TakeTxComplete(success); }
static srn_link_tx_result_t transport_submit(void *user, const uint8_t *data, size_t size)
{
    (void)user;
    switch (SRN_DebugPort_TryTransmit(data,size)) {
    case SRN_DEBUG_PORT_WRITE_OK: return SRN_LINK_TX_ACCEPTED;
    case SRN_DEBUG_PORT_WRITE_FULL: return SRN_LINK_TX_BUSY;
    case SRN_DEBUG_PORT_WRITE_NOT_CONFIGURED: return SRN_LINK_TX_DISCONNECTED;
    default: return SRN_LINK_TX_ERROR;
    }
}
static uint16_t storage_work(void *user, uint8_t service, uint8_t message,
 const uint8_t *request, size_t size, uint8_t *response, size_t capacity,
 size_t *length, uint8_t *flags)
{
    (void)user; (void)request; *flags=0;
    if (service != SRN_LINK_SERVICE_CONFIG || size != 0) { *length=0; return SRN_LINK_STATUS_BAD_LENGTH; }
    return TSKPTT_ParamService_RunStorage(message,response,capacity,length);
}
static bool defer_storage(void *user, uint8_t message, uint32_t sequence)
{
    (void)user;
    return TSKPTT_HostComm_SubmitDeferred(SRN_LINK_SERVICE_CONFIG,message,sequence,
     NULL,0,storage_work,NULL) == SRN_LINK_OK;
}
static srn_link_result_t setup_link(srn_link_t *link, void *user)
{
    (void)user;
    const tskptt_param_service_config_t params = { .defer=defer_storage, .user=NULL };
    srn_link_result_t result=TSKPTT_ParamService_Register(link,&params);
    if (result == SRN_LINK_OK && application_setup) result=application_setup(link,application_user);
    return result;
}
static void snapshot(const tskptt_host_comm_snapshot_t *s, void *user)
{
    (void)user;
    omx_dbg_begin(&g_omx_dbg_srn_link.h);
    g_omx_dbg_srn_link.comm_stack_high_water=s->comm_stack_high_water_words;
    g_omx_dbg_srn_link.worker_stack_high_water=s->worker_stack_high_water_words;
    g_omx_dbg_srn_link.deferred_depth=s->deferred_depth;
    g_omx_dbg_srn_link.deferred_high_water=s->deferred_high_water;
    g_omx_dbg_srn_link.source_registered=s->sources.registered_count;
    g_omx_dbg_srn_link.source_active=s->sources.active_count;
    omx_dbg_end(&g_omx_dbg_srn_link.h,HAL_GetTick(),0);
}
bool SRNOS_HostLinkPrepare(bool enable_usb, SRNOS_HostLinkSetupFn setup, void *user)
{
    if (!enable_usb || prepared || osKernelGetState()==osKernelRunning) return false;
    application_setup=setup; application_user=user;
    omx_dbg_zero(&g_omx_dbg_srn_link,sizeof(g_omx_dbg_srn_link));
    omx_dbg_init(&g_omx_dbg_srn_link.h,OMX_DBG_MODULE_SRN_LINK,0,sizeof(g_omx_dbg_srn_link));
    const tskptt_host_comm_config_t config = {
      .transport={NULL,transport_init,transport_start,transport_read,transport_overflow,
                  transport_submit,transport_complete,transport_connected},
      .now_ms=now_ms,.cycles=cycles,.clock_user=NULL,
      .tx_storage=tx_storage,.tx_storage_size=sizeof(tx_storage),
      .rx_drain_buffer=rx_drain,.rx_drain_buffer_size=sizeof(rx_drain),
      .project_setup=setup_link,.snapshot=snapshot,.project_user=NULL,
      .task_wait_ms=2,.snapshot_period_ms=100,
    };
    if (TSKPTT_HostComm_Configure(&config)!=SRN_LINK_OK ||
        TSKPTT_HostComm_PrepareLink()!=SRN_LINK_OK) return false;
    prepared=true; return true;
}
bool SRNOS_HostLinkStart(void)
{
    if (!prepared || comm_thread || command_thread) return false;
    if (!TSKPTT_HostComm_InitResources()) return false;
    const osThreadAttr_t command={.name="HostCmd",.stack_size=2048,.priority=osPriorityLow1};
    const osThreadAttr_t comm={.name="HostComm",.stack_size=2048,.priority=osPriorityNormal};
    command_thread=osThreadNew(TSKPTT_HostComm_WorkerTask,NULL,&command);
    if (!command_thread) return false;
    comm_thread=osThreadNew(TSKPTT_HostComm_Task,NULL,&comm);
    if (!comm_thread) { (void)osThreadTerminate(command_thread); command_thread=NULL; return false; }
    return true;
}
