#include "SirinEmbedOS/Kernel/Thread.h"
#include "thread_internal.h"
#include "FreeRTOS.h"
#include "task.h"
//
// exfdcan_tx_router.cpp - MCP2518FD TX backend for SRNOS_CANRouterCore
//
#include "EXFDCAN_Router_Internal.h"
#include "can_router_core_internal.h"
#include "Devices/MCP2518FD/vendor/drv_canfdspi_api.h"
#include "Devices/MCP2518FD/vendor/drv_canfdspi_defines.h"
#include "stm32h7xx_hal.h"

#include <cstdint>
#include <cstring>

/* SPI rejection need not produce any CAN TX IRQ. Retry only pending work,
 * with the existing oneshot TTL still bounding its overall lifetime. */
static void SRNOS_ExternalCANTxDeferRetry(SRNOS_ExternalCANCtx *ctx)
{
    ctx->tx_retry_after_ms=SRNOS_TimeMs()+1u;
    ctx->tx_retry_pending=true;
}

static bool SRNOS_ExternalCANChannelToDev(SRNOS_CANChannel ch, uint8_t *dev)
{
    const int mapped = SRNOS_ExternalCANDevFromChannel(ch);
    if ((mapped < 0) || (mapped >= (int)SRNOS_EXTERNAL_CAN_DEV_COUNT) || !dev)
        return false;
    *dev = (uint8_t)mapped;
    return true;
}

static bool SRNOS_ExternalCANBackendStart(SRNOS_CANChannel ch)
{
    uint8_t dev = 0u;
    if (!SRNOS_ExternalCANChannelToDev(ch, &dev))
        return false;
    return SRNOS_ExternalCANHwInit(dev) == 0;
}

static uint8_t SRNOS_ExternalCANBackendGetTxFree(SRNOS_CANChannel ch)
{
    uint8_t dev=0;if(!SRNOS_ExternalCANChannelToDev(ch,&dev))return 0;
    auto *ctx=SRNOS_ExternalCANCtxOf(dev);
    /* Admission is checked in the same SPI sequence as the RAM write. MCP
     * exposes a not-full bit, not a free count; 3 retains the existing core
     * periodic eligibility convention. In-flight ownership blocks new TX. */
    return ctx && !ctx->tx_fifo.pending && !ctx->tx_fifo.quarantined?3u:0u;
}
static bool SRNOS_ExternalCANBackendLoad(SRNOS_CANChannel ch,uint32_t id,bool extended,
                                        const uint8_t *data,uint8_t length,bool fd,bool brs)
{
    uint8_t dev=0;if(!SRNOS_ExternalCANChannelToDev(ch,&dev))return false;
    auto *ctx=SRNOS_ExternalCANCtxOf(dev);if(!ctx)return false;
    CAN_TX_MSGOBJ obj{};
    obj.bF.id.SID=extended?(id>>18)&0x7ffu:id&0x7ffu;
    obj.bF.id.EID=extended?id&0x3ffffu:0;
    obj.bF.ctrl.IDE=extended;obj.bF.ctrl.DLC=(CAN_DLC)SRNOS_CANFDLengthToDlc(length);
    obj.bF.ctrl.FDF=fd;obj.bF.ctrl.BRS=brs;
    uint32_t errors=ctx->tx_fifo.errors;
    bool accepted=SRNOS_MCPFifoTransmit(&ctx->tx_fifo,obj.byte,data,length);
    ctx->stats->last_tx_status=ctx->tx_fifo.status;
    ctx->stats->tx_errors+=ctx->tx_fifo.errors-errors;
    if(accepted) {++ctx->stats->tx_frames;ctx->tx_retry_pending=false;}
    else {
        if(!(ctx->tx_fifo.status&CAN_TX_FIFO_NOT_FULL))++ctx->stats->tx_full;
        SRNOS_ExternalCANTxDeferRetry(ctx);
    }
    return accepted;
}

static bool SRNOS_ExternalCANBackendTryPush(SRNOS_CANChannel ch,
                                     const SRNOS_CANFrame *frame,
                                     SRNOS_CANRouterTxClass tx_class)
{
    (void)tx_class;
    if(!frame || !SRNOS_CANControllerIsClassic(ch) ||
       (frame->flags & ~SRNOS_CAN_FRAME_FLAG_EXT) || frame->dlc>SRNOS_EXTERNAL_CAN_MAX_DATA_BYTES)
        return false;
    return SRNOS_ExternalCANBackendLoad(ch,frame->id,(frame->flags&SRNOS_CAN_FRAME_FLAG_EXT)!=0,
                                       frame->data,frame->dlc,false,false);
}

static bool SRNOS_ExternalCANFDBackendTryPush(SRNOS_CANChannel ch, const SRNOS_CANFDFrame *frame)
{
    const SRNOS_CANControllerConfig *config=SRNOS_CANControllerConfigOf(ch);
    if(!frame || !config || config->mode!=SRNOS_CAN_MODE_FD ||
       (frame->flags & ~(SRNOS_CANFD_FRAME_FLAG_EXT|SRNOS_CANFD_FRAME_FLAG_BRS)) ||
       ((frame->flags&SRNOS_CANFD_FRAME_FLAG_BRS)!=0)!=config->brs ||
       SRNOS_CANFDLengthToDlc(frame->length)==0xffu ||
       frame->id>((frame->flags&SRNOS_CANFD_FRAME_FLAG_EXT)?0x1fffffffu:0x7ffu))
        return false;
    return SRNOS_ExternalCANBackendLoad(ch,frame->id,(frame->flags&SRNOS_CANFD_FRAME_FLAG_EXT)!=0,
                                       frame->data,frame->length,true,config->brs);
}

static uint32_t SRNOS_ExternalCANTxRetryWait(SRNOS_CANChannel ch, uint32_t now_ms)
{
    uint8_t dev=0;
    if(!SRNOS_ExternalCANChannelToDev(ch,&dev))return UINT32_MAX;
    const SRNOS_ExternalCANCtx *ctx=SRNOS_ExternalCANCtxOf(dev);
    if(!ctx || !ctx->tx_retry_pending)return UINT32_MAX;
    int32_t remaining=(int32_t)(ctx->tx_retry_after_ms-now_ms);
    return remaining>0?(uint32_t)remaining:0u;
}

static void SRNOS_ExternalCANBackendSignalTx(SRNOS_CANChannel ch)
{
    uint8_t dev = 0u;
    if (!SRNOS_ExternalCANChannelToDev(ch, &dev))
        return;

    SRNOS_ExternalCANCtx *ctx = SRNOS_ExternalCANCtxOf(dev);
    if (!ctx)
        return;

    SRNOS_ExternalCANNotify(ctx, SRNOS_EXTERNAL_CAN_EVT_TX_ROUTER);
}

static uint32_t SRNOS_ExternalCANBackendNowMs(void)
{
    return SRNOS_TimeMs();
}

static const SRNOS_CANRouterBackendOps s_backend_ops = {
    .start = SRNOS_ExternalCANBackendStart,
    .get_tx_free = SRNOS_ExternalCANBackendGetTxFree,
    .try_push = SRNOS_ExternalCANBackendTryPush,
    .signal_tx = SRNOS_ExternalCANBackendSignalTx,
    .now_ms = SRNOS_ExternalCANBackendNowMs,
    .retry_wait_ms = SRNOS_ExternalCANTxRetryWait,
};

static const SRNOS_CANFDBackendOps s_fd_backend_ops = {
    .get_tx_free = SRNOS_ExternalCANBackendGetTxFree,
    .try_push = SRNOS_ExternalCANFDBackendTryPush,
    .signal_tx = SRNOS_ExternalCANBackendSignalTx,
    .now_ms = SRNOS_ExternalCANBackendNowMs,
    .retry_wait_ms = SRNOS_ExternalCANTxRetryWait,
};

extern "C" void SRNOS_ExternalCANBackendBind(uint8_t dev)
{
    const SRNOS_CANChannel ch = SRNOS_ExternalCANChannelFromDev(dev);
    if (SRNOS_CANControllerIsFD(ch))
        SRNOS_CANFDCoreSetBackend(ch, &s_fd_backend_ops);
    else if (SRNOS_CANControllerIsClassic(ch))
        SRNOS_CANRouterCoreSetBackend(ch, &s_backend_ops);
}
