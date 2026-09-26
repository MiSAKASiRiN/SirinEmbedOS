//
// tskptt_exfdcan_tx_router.cpp - MCP2518FD TX backend for can_router_core
//
#include "EXFDCAN_Router_Internal.h"
#include "can_router_core_internal.h"
#include "SirinEmbedOS/Devices/MCP2518FD/drv_canfdspi_api.h"
#include "SirinEmbedOS/Devices/MCP2518FD/drv_canfdspi_defines.h"
#include "stm32h7xx_hal.h"

#include <cstdint>
#include <cstring>

#ifndef EXFDCAN_TX_WAIT_MS
#define EXFDCAN_TX_WAIT_MS 1u
#endif

static bool exfdcan_channel_to_dev(can_channel_t ch, uint8_t *dev)
{
    const int mapped = exfdcan_dev_from_channel(ch);
    if ((mapped < 0) || (mapped >= (int)EXFDCAN_DEV_COUNT) || !dev)
        return false;
    *dev = (uint8_t)mapped;
    return true;
}

static bool exfdcan_backend_start(can_channel_t ch)
{
    uint8_t dev = 0u;
    if (!exfdcan_channel_to_dev(ch, &dev))
        return false;
    return exfdcan_hw_init(dev) == 0;
}

static uint8_t exfdcan_backend_get_tx_free(can_channel_t ch)
{
    uint8_t dev = 0u;
    if (!exfdcan_channel_to_dev(ch, &dev))
        return 0u;

    exfdcan_ctx_t *ctx = exfdcan_ctx_of(dev);
    if (!ctx || !ctx->stats)
        return 0u;

    CAN_TX_FIFO_STATUS status = CAN_TX_FIFO_FULL;
    exfdcan_bus_lock();
    if (DRV_CANFDSPI_TransmitChannelStatusGet(dev, EXFDCAN_TX_FIFO, &status) != 0)
    {
        ctx->stats->tx_errors++;
        exfdcan_bus_unlock();
        return 0u;
    }
    ctx->stats->last_tx_status = (uint32_t)status;
    exfdcan_bus_unlock();

    return (status & CAN_TX_FIFO_NOT_FULL) ? 3u : 0u;
}

static bool exfdcan_backend_try_push(can_channel_t ch,
                                     const can_frame_t *frame,
                                     can_router_tx_class_t tx_class)
{
    (void)tx_class;

    uint8_t dev = 0u;
    if (!exfdcan_channel_to_dev(ch, &dev) || !frame || !frame->data)
        return false;
    if ((frame->flags & CAN_FRAME_FLAG_RTR) || (frame->dlc > EXFDCAN_MAX_DATA_BYTES))
        return false;

    exfdcan_ctx_t *ctx = exfdcan_ctx_of(dev);
    if (!ctx || !ctx->stats)
        return false;

    CAN_TX_FIFO_STATUS status = CAN_TX_FIFO_FULL;
    CAN_TX_MSGOBJ txObj;

    exfdcan_bus_lock();
    if (DRV_CANFDSPI_TransmitChannelStatusGet(dev, EXFDCAN_TX_FIFO, &status) != 0)
    {
        ctx->stats->tx_errors++;
        exfdcan_bus_unlock();
        return false;
    }

    ctx->stats->last_tx_status = (uint32_t)status;
    if (!(status & CAN_TX_FIFO_NOT_FULL))
    {
        ctx->stats->tx_full++;
        ctx->stats->tx_errors++;
        exfdcan_bus_unlock();
        return false;
    }

    std::memset(&txObj, 0, sizeof(txObj));
    if (frame->flags & CAN_FRAME_FLAG_EXT)
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

    uint8_t buf[EXFDCAN_MAX_DATA_BYTES];
    for (uint8_t i = 0; i < EXFDCAN_MAX_DATA_BYTES; ++i)
        buf[i] = (i < frame->dlc) ? frame->data[i] : 0u;

    if (DRV_CANFDSPI_TransmitChannelLoad(dev,
                                         EXFDCAN_TX_FIFO,
                                         &txObj,
                                         buf,
                                         frame->dlc,
                                         true) == 0)
    {
        ctx->stats->tx_frames++;
        exfdcan_bus_unlock();
        return true;
    }

    ctx->stats->tx_errors++;
    exfdcan_bus_unlock();
    return false;
}

static void exfdcan_backend_signal_tx(can_channel_t ch)
{
    uint8_t dev = 0u;
    if (!exfdcan_channel_to_dev(ch, &dev))
        return;

    exfdcan_ctx_t *ctx = exfdcan_ctx_of(dev);
    if (!ctx)
        return;

    if (ctx->evt)
        (void)osEventFlagsSet(ctx->evt, EXFDCAN_EVT_TX_ROUTER);
    else
        ctx->pending_isr |= EXFDCAN_EVT_TX_ROUTER;
}

static uint32_t exfdcan_backend_now_ms(void)
{
    return HAL_GetTick();
}

static const can_router_backend_ops_t s_backend_ops = {
    .start = exfdcan_backend_start,
    .get_tx_free = exfdcan_backend_get_tx_free,
    .try_push = exfdcan_backend_try_push,
    .signal_tx = exfdcan_backend_signal_tx,
    .now_ms = exfdcan_backend_now_ms,
};

static void exfdcan_backend_bind(uint8_t dev)
{
    const can_channel_t ch = exfdcan_channel_from_dev(dev);
    if (ch != 0u)
        can_router_core_set_backend(ch, &s_backend_ops);
}

static void exfdcan_tx_handle_event(uint8_t dev, exfdcan_ctx_t *ctx)
{
    CAN_MODULE_EVENT events = CAN_NO_EVENT;
    CAN_TX_FIFO_STATUS status = CAN_TX_FIFO_FULL;

    exfdcan_bus_lock();
    if (DRV_CANFDSPI_ModuleEventGet(dev, &events) == 0)
    {
        ctx->stats->last_int_flags = (uint32_t)events;
        ctx->stats->module_events |= (uint32_t)events;
        if (!(events & CAN_TX_EVENT))
        {
            ctx->stats->tx_irq_no_txif++;
            goto exfdcan_tx_clear_and_exit;
        }
    }
    else
    {
        ctx->stats->tx_errors++;
        goto exfdcan_tx_clear_and_exit;
    }

    if (DRV_CANFDSPI_TransmitChannelStatusGet(dev, EXFDCAN_TX_FIFO, &status) == 0)
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

exfdcan_tx_clear_and_exit:
    (void)DRV_CANFDSPI_ModuleEventClear(dev,
                                        (CAN_MODULE_EVENT)(CAN_TX_EVENT |
                                                           CAN_TX_ATTEMPTS_EVENT));
    (void)DRV_CANFDSPI_TransmitChannelEventAttemptClear(dev, EXFDCAN_TX_FIFO);
    exfdcan_bus_unlock();
}

extern "C" void Start_EXFDCAN_TxRouter(void *argument)
{
    const uint8_t dev = (uint8_t)(uintptr_t)argument;
    exfdcan_ctx_t *ctx = exfdcan_ctx_of(dev);
    const can_channel_t ch = exfdcan_channel_from_dev(dev);
    if (!ctx || ch == 0u)
        return;

    exfdcan_backend_bind(dev);

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

    if (exfdcan_hw_init(dev) != 0)
    {
        ctx->stats->init_errors++;
        return;
    }

    const uint32_t tick_hz = osKernelGetTickFreq();
    const uint32_t wait_ticks = (EXFDCAN_TX_WAIT_MS * tick_hz + 999u) / 1000u;

    for (;;)
    {
        const uint32_t flags = osEventFlagsWait(ctx->evt,
                                                (EXFDCAN_EVT_TX |
                                                 EXFDCAN_EVT_INT |
                                                 EXFDCAN_EVT_TX_ROUTER),
                                                osFlagsWaitAny,
                                                (wait_ticks ? wait_ticks : 1u));

        const bool has_event = ((flags & osFlagsError) == 0u);
        if (has_event && (flags & (EXFDCAN_EVT_TX | EXFDCAN_EVT_INT)))
        {
            ctx->stats->tx_irq++;
            can_router_core_note_tx_complete(ch);
            exfdcan_tx_handle_event(dev, ctx);
        }

        can_router_core_drain_tx(ch, exfdcan_backend_now_ms());
    }
}
