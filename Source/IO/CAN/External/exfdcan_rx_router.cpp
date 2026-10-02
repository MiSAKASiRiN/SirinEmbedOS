#include "thread_internal.h"
//
// exfdcan_rx_router.cpp - MCP2518FD RX backend for SRNOS_CANRouterCore
//
#include "EXFDCAN_Router_Internal.h"
#include "can_router_core_internal.h"
#include "Devices/MCP2518FD/vendor/drv_canfdspi_api.h"
#include "Devices/MCP2518FD/vendor/drv_canfdspi_defines.h"
#include "stm32h7xx_hal.h"

#include <cstdint>

#define SRNOS_EXTERNAL_CAN_RX_WAIT_MS 5u

#ifndef SRNOS_EXTERNAL_CAN_RX_DRAIN_MAX
#define SRNOS_EXTERNAL_CAN_RX_DRAIN_MAX 32u
#endif

#ifndef SRNOS_EXTERNAL_CAN_RX_DRAIN_YIELD_MS
#define SRNOS_EXTERNAL_CAN_RX_DRAIN_YIELD_MS 0u
#endif

static inline void SRNOS_ExternalCANRxYield(uint32_t delay_ms)
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
        osThreadYield();
    else
        osDelay(delay_ms);
}

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
    ctx->stats->rx_frames++;
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

static bool SRNOS_ExternalCANRxDrainLocked(uint8_t dev,
                                    SRNOS_ExternalCANCtx *ctx,
                                    SRNOS_CANChannel ch)
{
    CAN_RX_FIFO_STATUS status = CAN_RX_FIFO_EMPTY;
    uint32_t drained = 0u;
    bool hit_limit = false;

    for (;;)
    {
        if (DRV_CANFDSPI_ReceiveChannelStatusGet(dev, SRNOS_EXTERNAL_CAN_RX_FIFO, &status) != 0)
        {
            ctx->stats->rx_status_errors++;
            ctx->stats->rx_errors++;
            break;
        }

        ctx->stats->last_rx_status = (uint32_t)status;
        if (!(status & CAN_RX_FIFO_NOT_EMPTY))
            break;

        CAN_RX_MSGOBJ rxObj;
        uint8_t data[SRNOS_EXTERNAL_CAN_MAX_DATA_BYTES];
        if (DRV_CANFDSPI_ReceiveMessageGet(dev, SRNOS_EXTERNAL_CAN_RX_FIFO, &rxObj, data, SRNOS_EXTERNAL_CAN_MAX_DATA_BYTES) != 0)
        {
            ctx->stats->rx_read_errors++;
            ctx->stats->rx_errors++;
            break;
        }

        drained++;
        uint8_t dlc_bytes = (uint8_t)DRV_CANFDSPI_DlcToDataBytes((CAN_DLC)rxObj.bF.ctrl.DLC);
        if (dlc_bytes > SRNOS_EXTERNAL_CAN_MAX_DATA_BYTES)
            dlc_bytes = SRNOS_EXTERNAL_CAN_MAX_DATA_BYTES;

        const SRNOS_CANFrame frame = SRNOS_ExternalCANFrameFromRx(&rxObj, data, dlc_bytes);
        SRNOS_ExternalCANBusUnlock();
        SRNOS_ExternalCANRxPush(ctx, (uint16_t)(frame.id & 0x7FFu), dlc_bytes, data);
        SRNOS_CANRouterCoreOnRxFrame(ch, &frame, HAL_GetTick());
        SRNOS_ExternalCANBusLock();

        if (drained >= SRNOS_EXTERNAL_CAN_RX_DRAIN_MAX)
        {
            hit_limit = true;
            break;
        }
    }

    if (status & CAN_RX_FIFO_OVERFLOW)
    {
        (void)DRV_CANFDSPI_ReceiveChannelEventOverflowClear(dev, SRNOS_EXTERNAL_CAN_RX_FIFO);
        ctx->stats->rx_hw_overflow++;
        ctx->stats->rx_errors++;
    }

    (void)DRV_CANFDSPI_ModuleEventClear(dev, CAN_RX_EVENT);
    return hit_limit;
}

static void SRNOS_ExternalCANRxHandleEvent(uint8_t dev,
                                    SRNOS_ExternalCANCtx *ctx,
                                    SRNOS_CANChannel ch)
{
    CAN_MODULE_EVENT events = CAN_NO_EVENT;
    bool hit_limit = false;

    SRNOS_ExternalCANBusLock();
    if (DRV_CANFDSPI_ModuleEventGet(dev, &events) != 0)
    {
        ctx->stats->rx_status_errors++;
        ctx->stats->rx_errors++;
        goto SRNOS_ExternalCANRxClearAndExit;
    }

    ctx->stats->last_int_flags = (uint32_t)events;
    ctx->stats->module_events |= (uint32_t)events;
    if (!(events & CAN_RX_EVENT))
    {
        if (events & CAN_RX_OVERFLOW_EVENT)
            ctx->stats->rx_hw_overflow++;
        ctx->stats->rx_irq_no_rxif++;
        goto SRNOS_ExternalCANRxClearAndExit;
    }

    hit_limit = SRNOS_ExternalCANRxDrainLocked(dev, ctx, ch);
SRNOS_ExternalCANRxClearAndExit:
    if (events & CAN_RX_OVERFLOW_EVENT)
        (void)DRV_CANFDSPI_ReceiveChannelEventOverflowClear(dev, SRNOS_EXTERNAL_CAN_RX_FIFO);
    (void)DRV_CANFDSPI_ModuleEventClear(dev,
                                        (CAN_MODULE_EVENT)(CAN_RX_EVENT |
                                                           CAN_RX_OVERFLOW_EVENT));
    SRNOS_ExternalCANBusUnlock();

    if (hit_limit)
    {
        ctx->stats->rx_drain_limit++;
        (void)osEventFlagsSet(ctx->evt, SRNOS_EXTERNAL_CAN_EVT_RX);
        SRNOS_ExternalCANRxYield(SRNOS_EXTERNAL_CAN_RX_DRAIN_YIELD_MS);
    }
}

static void SRNOS_ExternalCANRxPoll(uint8_t dev,
                            SRNOS_ExternalCANCtx *ctx,
                            SRNOS_CANChannel ch)
{
    bool hit_limit = false;
    SRNOS_ExternalCANBusLock();
    hit_limit = SRNOS_ExternalCANRxDrainLocked(dev, ctx, ch);
    SRNOS_ExternalCANBusUnlock();

    if (hit_limit)
    {
        ctx->stats->rx_drain_limit++;
        (void)osEventFlagsSet(ctx->evt, SRNOS_EXTERNAL_CAN_EVT_RX);
        SRNOS_ExternalCANRxYield(SRNOS_EXTERNAL_CAN_RX_DRAIN_YIELD_MS);
    }
}

extern "C" void SRNOS_ExternalCANRxRouterWorker(void *argument)
{
    const uint8_t dev = (uint8_t)(uintptr_t)argument;
    SRNOS_ExternalCANCtx *ctx = SRNOS_ExternalCANCtxOf(dev);
    const SRNOS_CANChannel ch = SRNOS_ExternalCANChannelFromDev(dev);
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

    if (SRNOS_ExternalCANHwInit(dev) != 0)
    {
        ctx->stats->init_errors++;
        return;
    }

    const uint32_t tick_hz = osKernelGetTickFreq();
    const uint32_t wait_ticks = (SRNOS_EXTERNAL_CAN_RX_WAIT_MS * tick_hz + 999u) / 1000u;

    uint32_t flags=0;
    auto wait=[&]()->bool {
        flags = osEventFlagsWait(ctx->evt,
                                                (SRNOS_EXTERNAL_CAN_EVT_RX | SRNOS_EXTERNAL_CAN_EVT_INT),
                                                osFlagsWaitAny,
                                                (wait_ticks ? wait_ticks : 1u));

        return true;
    };
    auto step=[&](){        const bool has_event = ((flags & osFlagsError) == 0u);
        if (has_event && (flags & (SRNOS_EXTERNAL_CAN_EVT_RX | SRNOS_EXTERNAL_CAN_EVT_INT)))
        {
            ctx->stats->rx_irq++;
            SRNOS_CANRouterCoreNoteRxWake(ch, true);
            SRNOS_ExternalCANRxHandleEvent(dev, ctx, ch);
        }
        else
        {
            SRNOS_CANRouterCoreNoteRxWake(ch, false);
            SRNOS_ExternalCANRxPoll(dev, ctx, ch);
        }

    };
    SRNOS_KernelRunCpp(step,wait);
}
