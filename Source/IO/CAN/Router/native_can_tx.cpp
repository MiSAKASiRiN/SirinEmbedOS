#include "thread_internal.h"
#include "native_can_backend_internal.h"

#include "IO/CAN/Native/fdcan_internal.h"
#include "cmsis_os2.h"
#include "stm32h7xx_hal.h"

#include <cstdint>

static SRNOS_NativeCANTaskCtx s_task_ctx[SRNOS_CAN_ROUTER_NATIVE_CHANNEL_COUNT + 1u];

static FDCAN_GlobalTypeDef *fdcan_inst_of(SRNOS_CANChannel ch)
{
    switch (ch) {
        case 1: return FDCAN1;
        case 2: return FDCAN2;
        case 3: return FDCAN3;
        default: return nullptr;
    }
}

extern "C" SRNOS_NativeCANTaskCtx *SRNOS_NativeCANTaskCtxOf(SRNOS_CANChannel ch)
{
    if ((ch == 0u) || (ch > SRNOS_CAN_ROUTER_NATIVE_CHANNEL_COUNT)) {
        return nullptr;
    }
    return &s_task_ctx[ch];
}

extern "C" uint32_t SRNOS_NativeCANBackendNowMs(void)
{
    return HAL_GetTick();
}

static bool SRNOS_NativeCANBackendStart(SRNOS_CANChannel ch)
{
    return SRNOS_FDCANStart(ch);
}

static uint8_t SRNOS_NativeCANBackendGetTxFree(SRNOS_CANChannel ch)
{
    FDCAN_GlobalTypeDef *inst = fdcan_inst_of(ch);
    if (!inst) {
        return 0u;
    }
    const uint32_t txfqs = inst->TXFQS;
    return (uint8_t)((txfqs & FDCAN_TXFQS_TFFL_Msk) >> FDCAN_TXFQS_TFFL_Pos);
}

static bool SRNOS_NativeCANBackendTryPush(SRNOS_CANChannel ch,
                                     const SRNOS_CANFrame *frame,
                                     SRNOS_CANRouterTxClass tx_class)
{
    (void)tx_class;
    if (!frame || frame->dlc > SRNOS_CAN_ROUTER_MAX_DATA_BYTES ||
        (frame->flags & SRNOS_CAN_FRAME_FLAG_RTR)) {
        return false;
    }

    if (frame->flags & SRNOS_CAN_FRAME_FLAG_EXT) {
        return SRNOS_FDCANTxPushExt(ch, frame->id, frame->data, frame->dlc);
    }
    return SRNOS_FDCANTxPush(ch, (uint16_t)frame->id, frame->data, frame->dlc);
}

extern "C" void SRNOS_NativeCANBackendSignalRx(SRNOS_CANChannel ch)
{
    SRNOS_NativeCANTaskCtx *ctx = SRNOS_NativeCANTaskCtxOf(ch);
    if (!ctx) {
        return;
    }
    if (ctx->rx_evt) {
        (void)osEventFlagsSet(ctx->rx_evt, SRNOS_NATIVE_CAN_EVT_RX);
    } else {
        ctx->pending_rx |= SRNOS_NATIVE_CAN_EVT_RX;
    }
}

extern "C" void SRNOS_NativeCANBackendSignalTx(SRNOS_CANChannel ch)
{
    SRNOS_NativeCANTaskCtx *ctx = SRNOS_NativeCANTaskCtxOf(ch);
    if (!ctx) {
        return;
    }
    if (ctx->tx_evt) {
        (void)osEventFlagsSet(ctx->tx_evt, SRNOS_NATIVE_CAN_EVT_TX);
    } else {
        ctx->pending_tx |= SRNOS_NATIVE_CAN_EVT_TX;
    }
}

static const SRNOS_CANRouterBackendOps s_backend_ops = {
    .start = SRNOS_NativeCANBackendStart,
    .get_tx_free = SRNOS_NativeCANBackendGetTxFree,
    .try_push = SRNOS_NativeCANBackendTryPush,
    .signal_tx = SRNOS_NativeCANBackendSignalTx,
    .now_ms = SRNOS_NativeCANBackendNowMs,
    .retry_wait_ms = nullptr,
};

extern "C" void SRNOS_NativeCANBackendEnsureBound(SRNOS_CANChannel ch)
{
    SRNOS_CANRouterCoreSetBackend(ch, &s_backend_ops);
}

extern "C" void SRNOS_FDCANOnTxEvent(uint8_t bus)
{
    SRNOS_CANRouterCoreNoteTxComplete(bus);
    SRNOS_NativeCANBackendSignalTx(bus);
}

extern "C" void SRNOS_NativeCANTxRouterWorker(void *argument)
{
    const SRNOS_CANChannel ch = (SRNOS_CANChannel)(uintptr_t)argument;
    SRNOS_NativeCANTaskCtx *ctx = SRNOS_NativeCANTaskCtxOf(ch);
    if (!ctx) {
        return;
    }

    SRNOS_NativeCANBackendEnsureBound(ch);

    if (!ctx->tx_evt) ctx->tx_evt = osEventFlagsNew(nullptr);
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

    uint32_t flags=0;
    auto wait=[&]()->bool {
        flags = osEventFlagsWait(ctx->tx_evt,
                                                SRNOS_NATIVE_CAN_EVT_TX,
                                                osFlagsWaitAny,
                                                SRNOS_CANRouterCoreNextTxWait(ch, SRNOS_NativeCANBackendNowMs()));

        return true;
    };
    auto step=[&](){        (void)flags;
        SRNOS_CANRouterCoreDrainTx(ch, SRNOS_NativeCANBackendNowMs());

    };
    SRNOS_KernelRunCpp(step,wait);
}
