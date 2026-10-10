#include "thread_internal.h"
#include "FreeRTOS.h"
#include "task.h"
//
// exfdcan_rx_router.cpp - MCP2518FD RX backend for SRNOS_CANRouterCore
//
#include "EXFDCAN_Router_Internal.h"
#include "can_router_core_internal.h"
#include "Devices/MCP2518FD/vendor/drv_canfdspi_api.h"
#include "Devices/MCP2518FD/vendor/drv_canfdspi_defines.h"
#include "stm32h7xx_hal.h"

#include <cstdint>
#include <cstring>
#include "SirinEmbedOS/Diagnostics/Time/Time.h"
#include "Devices/MCP2518FD/vendor/drv_canfdspi_register.h"

static void SRNOS_ExternalCANRxPush(SRNOS_ExternalCANCtx *ctx,
                            uint16_t sid,
                            uint8_t dlc,
                            const uint8_t *data)
{
    if (!ctx || !data)
        return;

    uint32_t ps = __get_PRIMASK(); __disable_irq();
    uint16_t next = (uint16_t)((ctx->rx_head + 1u) % SRNOS_EXTERNAL_CAN_RX_LOG_DEPTH);
    if (next == ctx->rx_tail)
    {
        ctx->rx_tail = (uint16_t)((ctx->rx_tail + 1u) % SRNOS_EXTERNAL_CAN_RX_LOG_DEPTH);
        ctx->stats->rx_overflow++;
    }

    SRNOS_ExternalCANRxFrame *slot = &ctx->rx_log[ctx->rx_head];
    slot->sid = sid;
    slot->dlc = dlc;
    for (uint8_t i = 0; i < SRNOS_EXTERNAL_CAN_MAX_DATA_BYTES; ++i)
        slot->data[i] = (i < dlc) ? data[i] : 0u;
    slot->timestamp_ms = HAL_GetTick();

    ctx->rx_head = next;
    if (!ps) __enable_irq();
}

static SRNOS_CANFrame SRNOS_ExternalCANFrameFromRx(const CAN_RX_MSGOBJ *rxObj,
                                         const uint8_t *data,
                                         uint8_t dlc)
{
    SRNOS_CANFrame frame{};

    if (rxObj->bF.ctrl.IDE)
    {
        frame.id = ((rxObj->bF.id.SID & 0x7FFu) << 18) |
                   (rxObj->bF.id.EID & 0x3FFFFu);
        frame.flags = SRNOS_CAN_FRAME_FLAG_EXT;
    }
    else
    {
        frame.id = rxObj->bF.id.SID & 0x7FFu;
        frame.flags = 0u;
    }

    if (rxObj->bF.ctrl.RTR)
        frame.flags |= SRNOS_CAN_FRAME_FLAG_RTR;

    frame.dlc = dlc;
    for (uint8_t i = 0; i < SRNOS_CAN_ROUTER_MAX_DATA_BYTES; ++i)
        frame.data[i] = (i < dlc) ? data[i] : 0u;
    return frame;
}

static int SRNOS_ExternalCANReceive(uint8_t dev,SRNOS_ExternalCANCtx *ctx,SRNOS_CANChannel ch)
{
    uint8_t object[72]{};
    int result=SRNOS_MCPFifoReceive(&ctx->rx_fifo,object);
    ctx->stats->last_rx_status=ctx->rx_fifo.status;
    if(ctx->rx_fifo.status&CAN_RX_FIFO_OVERFLOW) {
        ++ctx->stats->rx_hw_overflow;++ctx->stats->rx_errors;
        (void)DRV_CANFDSPI_ReceiveChannelEventOverflowClear(dev,SRNOS_EXTERNAL_CAN_RX_FIFO);
    }
    if(result<0) {++ctx->stats->rx_read_errors;++ctx->stats->rx_errors;return result;}
    if(!result)return 0;
    CAN_RX_MSGOBJ rxObj{};std::memcpy(rxObj.byte,object,8);
    const uint8_t *data=object+8;
    const bool fd=SRNOS_CANControllerIsFD(ch);
    const uint8_t length=SRNOS_CANFDDlcToLength((uint8_t)rxObj.bF.ctrl.DLC);
    if((rxObj.bF.ctrl.FDF!=0)!=fd || length==0xffu || (fd && rxObj.bF.ctrl.RTR)) {
        ++ctx->stats->rx_errors;
        if(fd)SRNOS_CANFDCoreNoteRxRejected(ch);else SRNOS_CANRouterCoreNoteRxRejected(ch);
    } else if(fd) {
        SRNOS_CANFDFrame frame{};
        frame.id=rxObj.bF.ctrl.IDE?((rxObj.bF.id.SID&0x7ffu)<<18)|(rxObj.bF.id.EID&0x3ffffu):rxObj.bF.id.SID&0x7ffu;
        frame.length=length;
        if(rxObj.bF.ctrl.IDE)frame.flags|=SRNOS_CANFD_FRAME_FLAG_EXT;
        if(rxObj.bF.ctrl.BRS)frame.flags|=SRNOS_CANFD_FRAME_FLAG_BRS;
        if(rxObj.bF.ctrl.ESI)frame.flags|=SRNOS_CANFD_FRAME_FLAG_ESI;
        std::memcpy(frame.data,data,length);++ctx->stats->rx_frames;
        SRNOS_CANFDCoreOnRxFrame(ch,&frame,HAL_GetTick());
    } else {
        uint8_t count=length>8?8:length;
        SRNOS_CANFrame frame=SRNOS_ExternalCANFrameFromRx(&rxObj,data,count);
        ++ctx->stats->rx_frames;SRNOS_ExternalCANRxPush(ctx,(uint16_t)(frame.id&0x7ffu),count,data);
        SRNOS_CANRouterCoreOnRxFrame(ch,&frame,HAL_GetTick());
    }
    return 1;
}

static SRNOS_ExternalCANHardware s_hardware;
static volatile uint32_t s_hardware_generation;
static volatile bool s_hardware_requested;
extern "C" bool SRNOS_ExternalCANReadHardware(uint8_t dev,SRNOS_ExternalCANHardware *out)
{
    auto *ctx=SRNOS_ExternalCANCtxOf(dev);
    if(!ctx || !ctx->init_done || !ctx->task || !out || __get_IPSR() || osThreadGetId()==ctx->task)return false;
    taskENTER_CRITICAL();uint32_t generation=s_hardware_generation;s_hardware_requested=true;taskEXIT_CRITICAL();
    SRNOS_ExternalCANNotify(ctx,SRNOS_EXTERNAL_CAN_EVT_INT);
    uint32_t deadline=HAL_GetTick()+50u;
    while((int32_t)(deadline-HAL_GetTick())>0) {
        taskENTER_CRITICAL();bool done=s_hardware_generation!=generation;
        if(done)*out=s_hardware;
        taskEXIT_CRITICAL();
        if(done)return out->ok!=0;
        osDelay(1);
    }
    return false;
}
static void SRNOS_ExternalCANHardwareCapture(uint8_t dev)
{
    if(!s_hardware_requested)return;
    SRNOS_ExternalCANHardware out{};
    uint8_t tec=0,rec=0;CAN_ERROR_STATE error{};CAN_RX_FIFO_STATUS rx{};CAN_TX_FIFO_STATUS tx{};
    bool ok=DRV_CANFDSPI_ErrorCountStateGet(dev,&tec,&rec,&error)==0;
    out.mode=DRV_CANFDSPI_OperationModeGet(dev);
    ok=(DRV_CANFDSPI_ReadWord(dev,cREGADDR_CiNBTCFG,&out.nbtp)==0)&&ok;
    ok=(DRV_CANFDSPI_ReadWord(dev,cREGADDR_CiDBTCFG,&out.dbtp)==0)&&ok;
    ok=(DRV_CANFDSPI_ReadWord(dev,cREGADDR_CiTDC,&out.tdc)==0)&&ok;
    ok=(DRV_CANFDSPI_ReceiveChannelStatusGet(dev,SRNOS_EXTERNAL_CAN_RX_FIFO,&rx)==0)&&ok;
    ok=(DRV_CANFDSPI_TransmitChannelStatusGet(dev,SRNOS_EXTERNAL_CAN_TX_FIFO,&tx)==0)&&ok;
    out.tec=tec;out.rec=rec;out.error_state=error;out.rx_status=rx;out.tx_status=tx;out.ok=ok;
    taskENTER_CRITICAL();s_hardware=out;++s_hardware_generation;s_hardware_requested=false;taskEXIT_CRITICAL();
}
struct SRNOS_ExternalCANService {
    uint8_t dev;SRNOS_ExternalCANCtx *ctx;SRNOS_CANChannel ch;
    bool fd,rx_ready,tx_ready;
    uint32_t flags;
};
static void SRNOS_ExternalCANCollect(SRNOS_ExternalCANService &s)
{
    uint32_t flags=0;(void)xTaskNotifyWait(0,UINT32_MAX,&flags,0);
    s.flags|=flags;
    if(flags&(SRNOS_EXTERNAL_CAN_EVT_RX|SRNOS_EXTERNAL_CAN_EVT_INT))s.rx_ready=true;
    if(flags&(SRNOS_EXTERNAL_CAN_EVT_TX|SRNOS_EXTERNAL_CAN_EVT_INT|SRNOS_EXTERNAL_CAN_EVT_TX_ROUTER))s.tx_ready=true;
}
static void SRNOS_ExternalCANServiceStep(SRNOS_ExternalCANService &s)
{
    auto *ctx=s.ctx;const uint64_t start=SRNOS_TimeGetUptimeUs();unsigned processed=0;
    ++ctx->rounds;
    SRNOS_ExternalCANCollect(s);
    bool irq=(s.flags&(SRNOS_EXTERNAL_CAN_EVT_RX|SRNOS_EXTERNAL_CAN_EVT_INT))!=0;
    if(irq)++ctx->stats->rx_irq;
    if(s.flags&SRNOS_EXTERNAL_CAN_EVT_TX)++ctx->stats->tx_irq;
    if(s.fd)SRNOS_CANFDCoreNoteRxWake(s.ch,irq);else SRNOS_CANRouterCoreNoteRxWake(s.ch,irq);
    s.flags=0;
    SRNOS_ExternalCANHardwareCapture(s.dev);
    if(ctx->tx_fifo.pending) {
        if(!SRNOS_MCPFifoReconcile(&ctx->tx_fifo)) {
            ctx->tx_retry_pending=true;ctx->tx_retry_after_ms=HAL_GetTick()+1u;
        }
    }
    for(;;) {
        bool congested=(ctx->rx_fifo.status&CAN_RX_FIFO_HALF_FULL)!=0;
        unsigned rx_budget=congested?8:4,tx_budget=congested?1:4;
        for(unsigned i=0;s.rx_ready && i<rx_budget;++i) {
#ifdef SRNOS_CAN4_PROFILE
            uint64_t before=SRNOS_TimeGetUptimeUs();
#endif
            int r=SRNOS_ExternalCANReceive(s.dev,ctx,s.ch);
#ifdef SRNOS_CAN4_PROFILE
            uint32_t us=(uint32_t)(SRNOS_TimeGetUptimeUs()-before);
            ctx->rx_service_us+=us;if(us>ctx->max_rx_us)ctx->max_rx_us=us;
#endif
            if(r!=1)s.rx_ready=false;else ++processed;
            if(ctx->rx_fifo.status&CAN_RX_FIFO_HALF_FULL) {rx_budget=8;tx_budget=1;}
            SRNOS_ExternalCANCollect(s);
            if(processed>=32 || SRNOS_TimeGetUptimeUs()-start>=1000)goto budget;
            if(r!=1)break;
        }
        for(unsigned i=0;s.tx_ready && i<tx_budget;++i) {
#ifdef SRNOS_CAN4_PROFILE
            uint64_t before=SRNOS_TimeGetUptimeUs();
#endif
            bool sent=s.fd?SRNOS_CANFDCoreStepTx(s.ch,HAL_GetTick()):SRNOS_CANRouterCoreStepTx(s.ch,HAL_GetTick());
#ifdef SRNOS_CAN4_PROFILE
            uint32_t us=(uint32_t)(SRNOS_TimeGetUptimeUs()-before);
            ctx->tx_service_us+=us;if(us>ctx->max_tx_us)ctx->max_tx_us=us;
#endif
            if(!sent)s.tx_ready=false;
            else {++processed;s.rx_ready=true;} /* Check RX before sleeping after a TX burst. */
            SRNOS_ExternalCANCollect(s);
            if(ctx->stats->last_tx_status&CAN_TX_FIFO_ATTEMPTS_EXHAUSTED)
                (void)DRV_CANFDSPI_TransmitChannelEventAttemptClear(s.dev,SRNOS_EXTERNAL_CAN_TX_FIFO);
            if(processed>=32 || SRNOS_TimeGetUptimeUs()-start>=1000)goto budget;
            if(!sent)break;
        }
        if(!s.rx_ready && !s.tx_ready)return;
    }
budget:
    ++ctx->stats->rx_drain_limit;
    osThreadYield(); /* Readiness survives; no fixed delay or rate limiting. */
}
static bool SRNOS_ExternalCANServiceWait(SRNOS_ExternalCANService &s)
{
    if(s.rx_ready || s.tx_ready)return true;
    uint32_t now=HAL_GetTick();
    uint32_t delay=s.fd?SRNOS_CANFDCoreNextTxWait(s.ch,now):SRNOS_CANRouterCoreNextTxWait(s.ch,now);
    if(delay>5)delay=5;
    if(s.ctx->rx_fifo.pending || s.ctx->tx_fifo.pending)delay=1;
    uint32_t flags=0;
    (void)xTaskNotifyWait(0,UINT32_MAX,&flags,(TickType_t)(((uint64_t)delay*configTICK_RATE_HZ+999)/1000));
    ++s.ctx->wakeups;s.flags|=flags;
    /* Timeouts also recheck both directions (lost-edge/idle fallback). Atomic
     * notify wait consumes existing events or sleeps; no clear/sleep gap. */
    s.rx_ready=s.tx_ready=true;return true;
}
extern "C" void SRNOS_ExternalCANWorker(void *argument)
{
    uint8_t dev=(uint8_t)(uintptr_t)argument;
    auto *ctx=SRNOS_ExternalCANCtxOf(dev);auto ch=SRNOS_ExternalCANChannelFromDev(dev);
    if(!ctx || !ch)return;
    SRNOS_ExternalCANBackendBind(dev);
    if(SRNOS_ExternalCANHwInit(dev)!=0)return;
    SRNOS_ExternalCANRegisterWorker(ctx);
    SRNOS_ExternalCANService s{dev,ctx,ch,SRNOS_CANControllerIsFD(ch),true,true,0};
    auto step=[&](){SRNOS_ExternalCANServiceStep(s);};
    auto wait=[&](){return SRNOS_ExternalCANServiceWait(s);};
    SRNOS_KernelRunCpp(step,wait);
}
