#include "ntfdcan_backend_internal.h"

#include "SirinEmbedOS/IO/CAN/Native/bsp_fdcan.h"
#include "cmsis_os2.h"
#include "stm32h7xx_hal.h"

#include <cstddef>
#include <cstdint>

#ifndef NTFDCAN_RX_WAIT_MS
#define NTFDCAN_RX_WAIT_MS 2u
#endif

#ifndef NTFDCAN_RX_BATCH
#define NTFDCAN_RX_BATCH 32u
#endif

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

static void route_rx_frame(can_channel_t ch, const bsp_fdcan_rx_frame_t *in)
{
    if (!in) {
        return;
    }

    can_frame_t frame = {};
    frame.id = in->hdr.Identifier;
    frame.dlc = decode_fdcan_dlc(in->hdr.DataLength);
    if (in->hdr.IdType == FDCAN_EXTENDED_ID) {
        frame.flags |= CAN_FRAME_FLAG_EXT;
    }
    if (in->hdr.RxFrameType == FDCAN_REMOTE_FRAME) {
        frame.flags |= CAN_FRAME_FLAG_RTR;
    }

    const uint8_t copy_len = (frame.dlc > CAN_ROUTER_MAX_DATA_BYTES) ?
                             CAN_ROUTER_MAX_DATA_BYTES : frame.dlc;
    for (uint8_t i = 0; i < copy_len; ++i) {
        frame.data[i] = in->data[i];
    }

    can_router_core_on_rx_frame(ch, &frame, ntfdcan_backend_now_ms());
}

extern "C" void bsp_fdcan_on_rx_isr(uint8_t bus)
{
    ntfdcan_backend_signal_rx(bus);
}

extern "C" void Start_CAN_RxRouter(void *argument)
{
    const can_channel_t ch = (can_channel_t)(uintptr_t)argument;
    ntfdcan_task_ctx_t *ctx = ntfdcan_task_ctx_of(ch);
    if (!ctx) {
        return;
    }

    ntfdcan_backend_ensure_bound(ch);

    if (!ctx->rx_evt) {
        if (!ctx->rx_evt) {
            ctx->rx_evt = osEventFlagsNew(nullptr);
        }
    }
    if (!ctx->rx_evt) {
        return;
    }

    if (ctx->pending_rx) {
        (void)osEventFlagsSet(ctx->rx_evt, ctx->pending_rx);
        ctx->pending_rx = 0u;
    }

    (void)bsp_fdcan_start(ch);

    bsp_fdcan_rx_frame_t frames[NTFDCAN_RX_BATCH];
    for (;;) {
        const uint32_t flags = osEventFlagsWait(ctx->rx_evt,
                                                NTFDCAN_EVT_RX,
                                                osFlagsWaitAny,
                                                NTFDCAN_RX_WAIT_MS);
        const bool by_isr = ((flags & osFlagsError) == 0u) &&
                            ((flags & NTFDCAN_EVT_RX) != 0u);
        can_router_core_note_rx_wake(ch, by_isr);
        (void)osEventFlagsClear(ctx->rx_evt, NTFDCAN_EVT_RX);

        for (;;) {
            const size_t n = bsp_fdcan_rx_pop(ch, frames, NTFDCAN_RX_BATCH);
            if (n == 0u) {
                break;
            }
            for (size_t i = 0; i < n; ++i) {
                route_rx_frame(ch, &frames[i]);
            }
        }
    }
}
