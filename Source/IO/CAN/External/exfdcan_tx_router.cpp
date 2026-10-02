#include "thread_internal.h"
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
    ctx->tx_retry_after_ms=HAL_GetTick()+1u;
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
    uint8_t dev = 0u;
    if (!SRNOS_ExternalCANChannelToDev(ch, &dev))
        return 0u;

    SRNOS_ExternalCANCtx *ctx = SRNOS_ExternalCANCtxOf(dev);
    if (!ctx || !ctx->stats)
        return 0u;

    ctx->tx_retry_pending=false;
    CAN_TX_FIFO_STATUS status = CAN_TX_FIFO_FULL;
    SRNOS_ExternalCANBusLock();
    if (DRV_CANFDSPI_TransmitChannelStatusGet(dev, SRNOS_EXTERNAL_CAN_TX_FIFO, &status) != 0)
    {
        ctx->stats->tx_errors++;
        SRNOS_ExternalCANBusUnlock();
        SRNOS_ExternalCANTxDeferRetry(ctx);
        return 0u;
    }
    ctx->stats->last_tx_status = (uint32_t)status;
    SRNOS_ExternalCANBusUnlock();

    return (status & CAN_TX_FIFO_NOT_FULL) ? 3u : 0u;
}

static bool SRNOS_ExternalCANBackendTryPush(SRNOS_CANChannel ch,
                                     const SRNOS_CANFrame *frame,
                                     SRNOS_CANRouterTxClass tx_class)
{
    (void)tx_class;

    uint8_t dev = 0u;
    if (!SRNOS_ExternalCANChannelToDev(ch, &dev) || !frame)
        return false;
    if ((frame->flags & SRNOS_CAN_FRAME_FLAG_RTR) || (frame->dlc > SRNOS_EXTERNAL_CAN_MAX_DATA_BYTES))
        return false;

    SRNOS_ExternalCANCtx *ctx = SRNOS_ExternalCANCtxOf(dev);
    if (!ctx || !ctx->stats)
        return false;

    ctx->tx_retry_pending=false;
    CAN_TX_FIFO_STATUS status = CAN_TX_FIFO_FULL;
    CAN_TX_MSGOBJ txObj;

    SRNOS_ExternalCANBusLock();
    if (DRV_CANFDSPI_TransmitChannelStatusGet(dev, SRNOS_EXTERNAL_CAN_TX_FIFO, &status) != 0)
    {
        ctx->stats->tx_errors++;
        SRNOS_ExternalCANBusUnlock();
        SRNOS_ExternalCANTxDeferRetry(ctx);
        return false;
    }

    ctx->stats->last_tx_status = (uint32_t)status;
    if (!(status & CAN_TX_FIFO_NOT_FULL))
    {
        ctx->stats->tx_full++;
        ctx->stats->tx_errors++;
        SRNOS_ExternalCANBusUnlock();
        return false;
    }

    std::memset(&txObj, 0, sizeof(txObj));
    if (frame->flags & SRNOS_CAN_FRAME_FLAG_EXT)
    {
        txObj.bF.id.SID = (frame->id >> 18) & 0x7FFu;
        txObj.bF.id.EID = frame->id & 0x3FFFFu;
        txObj.bF.ctrl.IDE = 1u;
    }
    else
    {
        txObj.bF.id.SID = frame->id & 0x7FFu;
        txObj.bF.ctrl.IDE = 0u;
    }
    txObj.bF.ctrl.DLC = (CAN_DLC)frame->dlc;
    txObj.bF.ctrl.RTR = 0u;
    txObj.bF.ctrl.FDF = 0u;
    txObj.bF.ctrl.BRS = 0u;

    uint8_t buf[SRNOS_EXTERNAL_CAN_MAX_DATA_BYTES];
    for (uint8_t i = 0; i < SRNOS_EXTERNAL_CAN_MAX_DATA_BYTES; ++i)
        buf[i] = (i < frame->dlc) ? frame->data[i] : 0u;

    if (DRV_CANFDSPI_TransmitChannelLoad(dev,
                                         SRNOS_EXTERNAL_CAN_TX_FIFO,
                                         &txObj,
                                         buf,
                                         frame->dlc,
                                         true) == 0)
    {
        ctx->stats->tx_frames++;
        SRNOS_ExternalCANBusUnlock();
        return true;
    }

    ctx->stats->tx_errors++;
    SRNOS_ExternalCANBusUnlock();
    SRNOS_ExternalCANTxDeferRetry(ctx);
    return false;
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

    if (ctx->evt)
        (void)osEventFlagsSet(ctx->evt, SRNOS_EXTERNAL_CAN_EVT_TX_ROUTER);
    else
        ctx->pending_isr |= SRNOS_EXTERNAL_CAN_EVT_TX_ROUTER;
}

static uint32_t SRNOS_ExternalCANBackendNowMs(void)
{
    return HAL_GetTick();
}

static const SRNOS_CANRouterBackendOps s_backend_ops = {
    .start = SRNOS_ExternalCANBackendStart,
    .get_tx_free = SRNOS_ExternalCANBackendGetTxFree,
    .try_push = SRNOS_ExternalCANBackendTryPush,
    .signal_tx = SRNOS_ExternalCANBackendSignalTx,
    .now_ms = SRNOS_ExternalCANBackendNowMs,
    .retry_wait_ms = SRNOS_ExternalCANTxRetryWait,
};

static void SRNOS_ExternalCANBackendBind(uint8_t dev)
{
    const SRNOS_CANChannel ch = SRNOS_ExternalCANChannelFromDev(dev);
    if (ch != 0u)
        SRNOS_CANRouterCoreSetBackend(ch, &s_backend_ops);
}

static void SRNOS_ExternalCANTxHandleEvent(uint8_t dev, SRNOS_ExternalCANCtx *ctx)
{
    CAN_MODULE_EVENT events = CAN_NO_EVENT;
    CAN_TX_FIFO_STATUS status = CAN_TX_FIFO_FULL;

    SRNOS_ExternalCANBusLock();
    if (DRV_CANFDSPI_ModuleEventGet(dev, &events) == 0)
    {
        ctx->stats->last_int_flags = (uint32_t)events;
        ctx->stats->module_events |= (uint32_t)events;
        if (!(events & CAN_TX_EVENT))
        {
            ctx->stats->tx_irq_no_txif++;
            goto SRNOS_ExternalCANTxClearAndExit;
        }
    }
    else
    {
        ctx->stats->tx_errors++;
        goto SRNOS_ExternalCANTxClearAndExit;
    }

    if (DRV_CANFDSPI_TransmitChannelStatusGet(dev, SRNOS_EXTERNAL_CAN_TX_FIFO, &status) == 0)
    {
        ctx->stats->last_tx_status = (uint32_t)status;
        if (!(status & CAN_TX_FIFO_NOT_FULL))
        {
            ctx->stats->tx_full++;
            ctx->stats->tx_errors++;
        }
    }
    else
    {
        ctx->stats->tx_errors++;
    }

SRNOS_ExternalCANTxClearAndExit:
    (void)DRV_CANFDSPI_ModuleEventClear(dev,
                                        (CAN_MODULE_EVENT)(CAN_TX_EVENT |
                                                           CAN_TX_ATTEMPTS_EVENT));
    (void)DRV_CANFDSPI_TransmitChannelEventAttemptClear(dev, SRNOS_EXTERNAL_CAN_TX_FIFO);
    SRNOS_ExternalCANBusUnlock();
}

extern "C" void SRNOS_ExternalCANTxRouterWorker(void *argument)
{
    const uint8_t dev = (uint8_t)(uintptr_t)argument;
    SRNOS_ExternalCANCtx *ctx = SRNOS_ExternalCANCtxOf(dev);
    const SRNOS_CANChannel ch = SRNOS_ExternalCANChannelFromDev(dev);
    if (!ctx || ch == 0u)
        return;

    SRNOS_ExternalCANBackendBind(dev);

    if (!ctx->evt)
    {
        if (!ctx->evt)
            ctx->evt = osEventFlagsNew(NULL);
    }
    if (!ctx->evt)
        return;

    if (ctx->pending_isr)
    {
        (void)osEventFlagsSet(ctx->evt, ctx->pending_isr);
        ctx->pending_isr = 0u;
    }

    if (SRNOS_ExternalCANHwInit(dev) != 0)
    {
        ctx->stats->init_errors++;
        return;
    }

    uint32_t flags=0;
    auto wait=[&]()->bool {
        flags = osEventFlagsWait(ctx->evt,
                                                (SRNOS_EXTERNAL_CAN_EVT_TX |
                                                 SRNOS_EXTERNAL_CAN_EVT_INT |
                                                 SRNOS_EXTERNAL_CAN_EVT_TX_ROUTER),
                                                osFlagsWaitAny,
                                                SRNOS_CANRouterCoreNextTxWait(ch, SRNOS_ExternalCANBackendNowMs()));


        return true;
    };
    auto step=[&](){        const bool has_event = ((flags & osFlagsError) == 0u);
        if (has_event && (flags & (SRNOS_EXTERNAL_CAN_EVT_TX | SRNOS_EXTERNAL_CAN_EVT_INT)))
        {
            ctx->stats->tx_irq++;
            SRNOS_CANRouterCoreNoteTxComplete(ch);
            SRNOS_ExternalCANTxHandleEvent(dev, ctx);
        }

        SRNOS_CANRouterCoreDrainTx(ch, SRNOS_ExternalCANBackendNowMs());

    };
    SRNOS_KernelRunCpp(step,wait);
}
