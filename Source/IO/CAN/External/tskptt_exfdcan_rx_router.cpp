//
// tskptt_exfdcan_rx_router.cpp - MCP2518FD RX backend for can_router_core
//
#include "EXFDCAN_Router_Internal.h"
#include "can_router_core_internal.h"
#include "SirinEmbedOS/Devices/MCP2518FD/drv_canfdspi_api.h"
#include "SirinEmbedOS/Devices/MCP2518FD/drv_canfdspi_defines.h"
#include "stm32h7xx_hal.h"

#include <cstdint>

#define EXFDCAN_RX_WAIT_MS 5u

#ifndef EXFDCAN_RX_DRAIN_MAX
#define EXFDCAN_RX_DRAIN_MAX 32u
#endif

#ifndef EXFDCAN_RX_DRAIN_YIELD_MS
#define EXFDCAN_RX_DRAIN_YIELD_MS 0u
#endif

static inline void exfdcan_rx_yield(uint32_t delay_ms)
{
    if (__get_IPSR() != 0U)
    {
        __NOP();
        return;
    }
    if (osKernelGetState() != osKernelRunning)
    {
        __NOP();
        return;
    }
    if (delay_ms == 0u)
        osDelay(0);
    else
        osDelay(delay_ms);
}

static void exfdcan_rx_push(exfdcan_ctx_t *ctx,
                            uint16_t sid,
                            uint8_t dlc,
                            const uint8_t *data)
{
    if (!ctx || !data)
        return;

    uint32_t ps = __get_PRIMASK(); __disable_irq();
    uint16_t next = (uint16_t)((ctx->rx_head + 1u) % EXFDCAN_RX_LOG_DEPTH);
    if (next == ctx->rx_tail)
    {
        ctx->rx_tail = (uint16_t)((ctx->rx_tail + 1u) % EXFDCAN_RX_LOG_DEPTH);
        ctx->stats->rx_overflow++;
    }

    exfdcan_rx_frame_t *slot = &ctx->rx_log[ctx->rx_head];
    slot->sid = sid;
    slot->dlc = dlc;
    for (uint8_t i = 0; i < EXFDCAN_MAX_DATA_BYTES; ++i)
        slot->data[i] = (i < dlc) ? data[i] : 0u;
    slot->timestamp_ms = HAL_GetTick();

    ctx->rx_head = next;
    ctx->stats->rx_frames++;
    if (!ps) __enable_irq();
}

static can_frame_t exfdcan_frame_from_rx(const CAN_RX_MSGOBJ *rxObj,
                                         const uint8_t *data,
                                         uint8_t dlc)
{
    can_frame_t frame{};

    if (rxObj->bF.ctrl.IDE)
    {
        frame.id = ((rxObj->bF.id.SID & 0x7FFu) << 18) |
                   (rxObj->bF.id.EID & 0x3FFFFu);
        frame.flags = CAN_FRAME_FLAG_EXT;
    }
    else
    {
        frame.id = rxObj->bF.id.SID & 0x7FFu;
        frame.flags = 0u;
    }

    if (rxObj->bF.ctrl.RTR)
        frame.flags |= CAN_FRAME_FLAG_RTR;

    frame.dlc = dlc;
    for (uint8_t i = 0; i < CAN_ROUTER_MAX_DATA_BYTES; ++i)
        frame.data[i] = (i < dlc) ? data[i] : 0u;
    return frame;
}

static bool exfdcan_rx_drain_locked(uint8_t dev,
                                    exfdcan_ctx_t *ctx,
                                    can_channel_t ch)
{
    CAN_RX_FIFO_STATUS status = CAN_RX_FIFO_EMPTY;
    uint32_t drained = 0u;
    bool hit_limit = false;

    for (;;)
    {
        if (DRV_CANFDSPI_ReceiveChannelStatusGet(dev, EXFDCAN_RX_FIFO, &status) != 0)
        {
            ctx->stats->rx_status_errors++;
            ctx->stats->rx_errors++;
            break;
        }

        ctx->stats->last_rx_status = (uint32_t)status;
        if (!(status & CAN_RX_FIFO_NOT_EMPTY))
            break;

        CAN_RX_MSGOBJ rxObj;
        uint8_t data[EXFDCAN_MAX_DATA_BYTES];
        if (DRV_CANFDSPI_ReceiveMessageGet(dev, EXFDCAN_RX_FIFO, &rxObj, data, EXFDCAN_MAX_DATA_BYTES) != 0)
        {
            ctx->stats->rx_read_errors++;
            ctx->stats->rx_errors++;
            break;
        }

        drained++;
        uint8_t dlc_bytes = (uint8_t)DRV_CANFDSPI_DlcToDataBytes((CAN_DLC)rxObj.bF.ctrl.DLC);
        if (dlc_bytes > EXFDCAN_MAX_DATA_BYTES)
            dlc_bytes = EXFDCAN_MAX_DATA_BYTES;

        const can_frame_t frame = exfdcan_frame_from_rx(&rxObj, data, dlc_bytes);
        exfdcan_bus_unlock();
        exfdcan_rx_push(ctx, (uint16_t)(frame.id & 0x7FFu), dlc_bytes, data);
        can_router_core_on_rx_frame(ch, &frame, HAL_GetTick());
        exfdcan_bus_lock();

        if (drained >= EXFDCAN_RX_DRAIN_MAX)
        {
            hit_limit = true;
            break;
        }
    }

    if (status & CAN_RX_FIFO_OVERFLOW)
    {
        (void)DRV_CANFDSPI_ReceiveChannelEventOverflowClear(dev, EXFDCAN_RX_FIFO);
        ctx->stats->rx_hw_overflow++;
        ctx->stats->rx_errors++;
    }

    (void)DRV_CANFDSPI_ModuleEventClear(dev, CAN_RX_EVENT);
    return hit_limit;
}

static void exfdcan_rx_handle_event(uint8_t dev,
                                    exfdcan_ctx_t *ctx,
                                    can_channel_t ch)
{
    CAN_MODULE_EVENT events = CAN_NO_EVENT;
    bool hit_limit = false;

    exfdcan_bus_lock();
    if (DRV_CANFDSPI_ModuleEventGet(dev, &events) != 0)
    {
        ctx->stats->rx_status_errors++;
        ctx->stats->rx_errors++;
        goto exfdcan_rx_clear_and_exit;
    }

    ctx->stats->last_int_flags = (uint32_t)events;
    ctx->stats->module_events |= (uint32_t)events;
    if (!(events & CAN_RX_EVENT))
    {
        if (events & CAN_RX_OVERFLOW_EVENT)
            ctx->stats->rx_hw_overflow++;
        ctx->stats->rx_irq_no_rxif++;
        goto exfdcan_rx_clear_and_exit;
    }

    hit_limit = exfdcan_rx_drain_locked(dev, ctx, ch);
exfdcan_rx_clear_and_exit:
    if (events & CAN_RX_OVERFLOW_EVENT)
        (void)DRV_CANFDSPI_ReceiveChannelEventOverflowClear(dev, EXFDCAN_RX_FIFO);
    (void)DRV_CANFDSPI_ModuleEventClear(dev,
                                        (CAN_MODULE_EVENT)(CAN_RX_EVENT |
                                                           CAN_RX_OVERFLOW_EVENT));
    exfdcan_bus_unlock();

    if (hit_limit)
    {
        ctx->stats->rx_drain_limit++;
        exfdcan_rx_yield(EXFDCAN_RX_DRAIN_YIELD_MS);
    }
}

static void exfdcan_rx_poll(uint8_t dev,
                            exfdcan_ctx_t *ctx,
                            can_channel_t ch)
{
    bool hit_limit = false;
    exfdcan_bus_lock();
    hit_limit = exfdcan_rx_drain_locked(dev, ctx, ch);
    exfdcan_bus_unlock();

    if (hit_limit)
    {
        ctx->stats->rx_drain_limit++;
        exfdcan_rx_yield(EXFDCAN_RX_DRAIN_YIELD_MS);
    }
}

extern "C" void Start_EXFDCAN_RxRouter(void *argument)
{
    const uint8_t dev = (uint8_t)(uintptr_t)argument;
    exfdcan_ctx_t *ctx = exfdcan_ctx_of(dev);
    const can_channel_t ch = exfdcan_channel_from_dev(dev);
    if (!ctx || ch == 0u)
        return;

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
    const uint32_t wait_ticks = (EXFDCAN_RX_WAIT_MS * tick_hz + 999u) / 1000u;

    for (;;)
    {
        const uint32_t flags = osEventFlagsWait(ctx->evt,
                                                (EXFDCAN_EVT_RX | EXFDCAN_EVT_INT),
                                                osFlagsWaitAny,
                                                (wait_ticks ? wait_ticks : 1u));
        const bool has_event = ((flags & osFlagsError) == 0u);
        if (has_event && (flags & (EXFDCAN_EVT_RX | EXFDCAN_EVT_INT)))
        {
            ctx->stats->rx_irq++;
            can_router_core_note_rx_wake(ch, true);
            exfdcan_rx_handle_event(dev, ctx, ch);
        }
        else
        {
            can_router_core_note_rx_wake(ch, false);
            exfdcan_rx_poll(dev, ctx, ch);
        }
    }
}
