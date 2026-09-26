#include "ntfdcan_backend_internal.h"

#include "SirinEmbedOS/IO/CAN/Native/bsp_fdcan.h"
#include "cmsis_os2.h"
#include "stm32h7xx_hal.h"

#include <cstdint>

#ifndef NTFDCAN_TX_WAIT_MS
#define NTFDCAN_TX_WAIT_MS 1u
#endif

static ntfdcan_task_ctx_t s_task_ctx[CAN_ROUTER_NATIVE_CHANNEL_COUNT + 1u];

static inline uint32_t enter_critical(void)
{
    uint32_t ps = __get_PRIMASK();
    __disable_irq();
    return ps;
}

static inline void exit_critical(uint32_t ps)
{
    if (!ps) {
        __enable_irq();
    }
}

static FDCAN_GlobalTypeDef *fdcan_inst_of(can_channel_t ch)
{
    switch (ch) {
        case 1: return FDCAN1;
        case 2: return FDCAN2;
        case 3: return FDCAN3;
        default: return nullptr;
    }
}

extern "C" ntfdcan_task_ctx_t *ntfdcan_task_ctx_of(can_channel_t ch)
{
    if ((ch == 0u) || (ch > CAN_ROUTER_NATIVE_CHANNEL_COUNT)) {
        return nullptr;
    }
    return &s_task_ctx[ch];
}

extern "C" uint32_t ntfdcan_backend_now_ms(void)
{
    return HAL_GetTick();
}

static bool ntfdcan_backend_start(can_channel_t ch)
{
    return bsp_fdcan_start(ch);
}

static uint8_t ntfdcan_backend_get_tx_free(can_channel_t ch)
{
    FDCAN_GlobalTypeDef *inst = fdcan_inst_of(ch);
    if (!inst) {
        return 0u;
    }
    const uint32_t txfqs = inst->TXFQS;
    return (uint8_t)((txfqs & FDCAN_TXFQS_TFFL_Msk) >> FDCAN_TXFQS_TFFL_Pos);
}

static bool ntfdcan_backend_try_push(can_channel_t ch,
                                     const can_frame_t *frame,
                                     can_router_tx_class_t tx_class)
{
    (void)tx_class;
    if (!frame || frame->dlc > CAN_ROUTER_MAX_DATA_BYTES ||
        (frame->flags & CAN_FRAME_FLAG_RTR)) {
        return false;
    }

    if (frame->flags & CAN_FRAME_FLAG_EXT) {
        return bsp_fdcan_tx_push_ext(ch, frame->id, frame->data, frame->dlc);
    }
    return bsp_fdcan_tx_push(ch, (uint16_t)frame->id, frame->data, frame->dlc);
}

extern "C" void ntfdcan_backend_signal_rx(can_channel_t ch)
{
    ntfdcan_task_ctx_t *ctx = ntfdcan_task_ctx_of(ch);
    if (!ctx) {
        return;
    }
    if (ctx->rx_evt) {
        (void)osEventFlagsSet(ctx->rx_evt, NTFDCAN_EVT_RX);
    } else {
        ctx->pending_rx |= NTFDCAN_EVT_RX;
    }
}

extern "C" void ntfdcan_backend_signal_tx(can_channel_t ch)
{
    ntfdcan_task_ctx_t *ctx = ntfdcan_task_ctx_of(ch);
    if (!ctx) {
        return;
    }
    if (ctx->tx_evt) {
        (void)osEventFlagsSet(ctx->tx_evt, NTFDCAN_EVT_TX);
    } else {
        ctx->pending_tx |= NTFDCAN_EVT_TX;
    }
}

static const can_router_backend_ops_t s_backend_ops = {
    .start = ntfdcan_backend_start,
    .get_tx_free = ntfdcan_backend_get_tx_free,
    .try_push = ntfdcan_backend_try_push,
    .signal_tx = ntfdcan_backend_signal_tx,
    .now_ms = ntfdcan_backend_now_ms,
};

extern "C" void ntfdcan_backend_ensure_bound(can_channel_t ch)
{
    can_router_core_set_backend(ch, &s_backend_ops);
}

extern "C" void bsp_fdcan_on_tx_event(uint8_t bus)
{
    can_router_core_note_tx_complete(bus);
    ntfdcan_backend_signal_tx(bus);
}

extern "C" void Start_CAN_TxRouter(void *argument)
{
    const can_channel_t ch = (can_channel_t)(uintptr_t)argument;
    ntfdcan_task_ctx_t *ctx = ntfdcan_task_ctx_of(ch);
    if (!ctx) {
        return;
    }

    ntfdcan_backend_ensure_bound(ch);

    if (!ctx->tx_evt) {
        if (!ctx->tx_evt) {
            ctx->tx_evt = osEventFlagsNew(nullptr);
        }
    }
    if (!ctx->tx_evt) {
        return;
    }

    if (ctx->pending_tx) {
        (void)osEventFlagsSet(ctx->tx_evt, ctx->pending_tx);
        ctx->pending_tx = 0u;
    }

    if (s_backend_ops.start) {
        (void)s_backend_ops.start(ch);
    }

    for (;;) {
        const uint32_t flags = osEventFlagsWait(ctx->tx_evt,
                                                NTFDCAN_EVT_TX,
                                                osFlagsWaitAny,
                                                NTFDCAN_TX_WAIT_MS);
        (void)flags;
        (void)osEventFlagsClear(ctx->tx_evt, NTFDCAN_EVT_TX);
        can_router_core_drain_tx(ch, ntfdcan_backend_now_ms());
    }
}
