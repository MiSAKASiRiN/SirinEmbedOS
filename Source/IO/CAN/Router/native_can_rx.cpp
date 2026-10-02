#include "thread_internal.h"
#include "native_can_backend_internal.h"

#include "IO/CAN/Native/fdcan_internal.h"
#include "cmsis_os2.h"
#include "stm32h7xx_hal.h"

#include <cstddef>
#include <cstdint>

#ifndef SRNOS_NATIVE_CAN_RX_BATCH
#define SRNOS_NATIVE_CAN_RX_BATCH 32u
#endif
static_assert(SRNOS_NATIVE_CAN_RX_BATCH>0 && SRNOS_NATIVE_CAN_RX_BATCH<=32, "native RX batch limit");

static uint8_t decode_fdcan_dlc(uint32_t data_length)
{
    static const uint8_t lut[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
    const uint8_t code_hi = (uint8_t)((data_length >> 16) & 0x0Fu);
    const uint8_t code_lo = (uint8_t)(data_length & 0x0Fu);

    if (code_hi < 16u && code_hi != 0u) {
        return lut[code_hi];
    }
    if (code_lo < 16u && code_lo != 0u) {
        return lut[code_lo];
    }
    if (data_length <= 64u) {
        return (uint8_t)data_length;
    }
    return 8u;
}

static void route_rx_frame(SRNOS_CANChannel ch, const SRNOS_FDCANRxFrame *in)
{
    if (!in) {
        return;
    }

    SRNOS_CANFrame frame = {};
    frame.id = in->hdr.Identifier;
    frame.dlc = decode_fdcan_dlc(in->hdr.DataLength);
    if (in->hdr.IdType == FDCAN_EXTENDED_ID) {
        frame.flags |= SRNOS_CAN_FRAME_FLAG_EXT;
    }
    if (in->hdr.RxFrameType == FDCAN_REMOTE_FRAME) {
        frame.flags |= SRNOS_CAN_FRAME_FLAG_RTR;
    }

    const uint8_t copy_len = (frame.dlc > SRNOS_CAN_ROUTER_MAX_DATA_BYTES) ?
                             SRNOS_CAN_ROUTER_MAX_DATA_BYTES : frame.dlc;
    for (uint8_t i = 0; i < copy_len; ++i) {
        frame.data[i] = in->data[i];
    }

    SRNOS_CANRouterCoreOnRxFrame(ch, &frame, SRNOS_NativeCANBackendNowMs());
}

extern "C" void SRNOS_FDCANOnRxISR(uint8_t bus)
{
    SRNOS_NativeCANBackendSignalRx(bus);
}

extern "C" void SRNOS_NativeCANRxRouterWorker(void *argument)
{
    const SRNOS_CANChannel ch = (SRNOS_CANChannel)(uintptr_t)argument;
    SRNOS_NativeCANTaskCtx *ctx = SRNOS_NativeCANTaskCtxOf(ch);
    if (!ctx) {
        return;
    }

    SRNOS_NativeCANBackendEnsureBound(ch);

    if (!ctx->rx_evt) ctx->rx_evt = osEventFlagsNew(nullptr);
    if (!ctx->rx_evt) {
        return;
    }

    if (ctx->pending_rx) {
        (void)osEventFlagsSet(ctx->rx_evt, ctx->pending_rx);
        ctx->pending_rx = 0u;
    }

    (void)SRNOS_FDCANStart(ch);

    SRNOS_FDCANRxFrame frames[SRNOS_NATIVE_CAN_RX_BATCH];
    uint32_t flags=0;
    auto wait=[&]()->bool {
        flags = osEventFlagsWait(ctx->rx_evt,
                                                SRNOS_NATIVE_CAN_EVT_RX,
                                                osFlagsWaitAny,
                                                osWaitForever);

        return true;
    };
    auto step=[&](){        const bool by_isr = ((flags & osFlagsError) == 0u) &&
                            ((flags & SRNOS_NATIVE_CAN_EVT_RX) != 0u);
        SRNOS_CANRouterCoreNoteRxWake(ch, by_isr);

        const size_t n = SRNOS_FDCANRxPop(ch, frames, SRNOS_NATIVE_CAN_RX_BATCH);
        for (size_t i = 0; i < n; ++i) route_rx_frame(ch, &frames[i]);
        if(n == SRNOS_NATIVE_CAN_RX_BATCH) {
            /* Retain readiness after the bounded batch, including level IRQs
             * which need not produce another edge while data remains. */
            (void)osEventFlagsSet(ctx->rx_evt, SRNOS_NATIVE_CAN_EVT_RX);
            (void)osThreadYield();
        }

    };
    SRNOS_KernelRunCpp(step,wait);
}
