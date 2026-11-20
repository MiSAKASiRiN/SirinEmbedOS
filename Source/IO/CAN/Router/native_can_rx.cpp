#include "thread_internal.h"
#include "native_can_backend_internal.h"

#include "IO/CAN/Native/fdcan_internal.h"
#include "../can_controller_internal.h"
#include "../FD/can_fd_internal.h"
#include "cmsis_os2.h"
#include "../Native/fdcan_hal.h"

#include <cstddef>
#include <cstdint>

#ifndef SRNOS_NATIVE_CAN_RX_BATCH
#define SRNOS_NATIVE_CAN_RX_BATCH 32u
#endif
static_assert(SRNOS_NATIVE_CAN_RX_BATCH>0 && SRNOS_NATIVE_CAN_RX_BATCH<=32, "native RX batch limit");

static void route_rx_frame(SRNOS_CANChannel ch, const SRNOS_FDCANRxFrame *in)
{
    if (!in) {
        return;
    }

    const bool fd = SRNOS_CANControllerIsFD(ch);
    /* The native ring carries the normalized four-bit DLC. FDF is required even
     * for 0..8-byte FD frames; never reinterpret them as a Classic protocol. */
    const uint8_t length = in->hdr.DataLength <= 15u ?
        SRNOS_CANFDDlcToLength((uint8_t)in->hdr.DataLength) : 0xffu;
    if (length == 0xffu || (!fd && length > 8u) ||
        in->hdr.RxFrameType != FDCAN_DATA_FRAME ||
        in->hdr.FDFormat != (fd ? FDCAN_FD_CAN : FDCAN_CLASSIC_CAN)) {
        if (fd) SRNOS_CANFDCoreNoteRxRejected(ch);
        else SRNOS_CANRouterCoreNoteRxRejected(ch);
        return;
    }
    if (fd) {
        SRNOS_CANFDFrame frame = {};
        frame.id = in->hdr.Identifier;
        frame.length = length;
        if (in->hdr.IdType == FDCAN_EXTENDED_ID) frame.flags |= SRNOS_CANFD_FRAME_FLAG_EXT;
        if (in->hdr.BitRateSwitch == FDCAN_BRS_ON) frame.flags |= SRNOS_CANFD_FRAME_FLAG_BRS;
        if (in->hdr.ErrorStateIndicator == FDCAN_ESI_PASSIVE) frame.flags |= SRNOS_CANFD_FRAME_FLAG_ESI;
        for (uint8_t i = 0u; i < length; ++i) frame.data[i] = in->data[i];
        SRNOS_CANFDCoreOnRxFrame(ch, &frame, SRNOS_NativeCANBackendNowMs());
        return;
    }

    SRNOS_CANFrame frame = {};
    frame.id = in->hdr.Identifier;
    frame.dlc = length;
    if (in->hdr.IdType == FDCAN_EXTENDED_ID) {
        frame.flags |= SRNOS_CAN_FRAME_FLAG_EXT;
    }
    for (uint8_t i = 0; i < length; ++i) {
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

    __atomic_store_n(&ctx->rx_thread,xTaskGetCurrentTaskHandle(),__ATOMIC_RELEASE);

    (void)SRNOS_FDCANStart(ch);

    /* Keep the scheduling budget at 32 without reserving 32 * 108 bytes on
     * the 4 KiB worker stack. Four-frame pop chunks leave room for callbacks,
     * TLS and the RTOS exception context; the ring remains the queue owner. */
    SRNOS_FDCANRxFrame frames[4];
    uint32_t flags=0;
    auto wait=[&]()->bool {
        (void)xTaskNotifyWait(0,UINT32_MAX,&flags,portMAX_DELAY);
        return true;
    };
    auto step=[&](){
        const bool by_isr = ((flags & osFlagsError) == 0u) &&
                            ((flags & SRNOS_NATIVE_CAN_EVT_RX) != 0u);
        if (SRNOS_CANControllerIsFD(ch)) SRNOS_CANFDCoreNoteRxWake(ch, by_isr);
        else SRNOS_CANRouterCoreNoteRxWake(ch, by_isr);

        size_t total = 0u;
        while (total < SRNOS_NATIVE_CAN_RX_BATCH) {
            const size_t remaining = SRNOS_NATIVE_CAN_RX_BATCH - total;
            const size_t capacity = remaining < 4u ? remaining : 4u;
            const size_t n = SRNOS_FDCANRxPop(ch, frames, capacity);
            for (size_t i = 0; i < n; ++i) route_rx_frame(ch, &frames[i]);
            total += n;
            if (n < capacity) break;
        }
        if(total == SRNOS_NATIVE_CAN_RX_BATCH) {
            /* Retain readiness after the bounded batch, including level IRQs
             * which need not produce another edge while data remains. */
            SRNOS_NativeCANBackendSignalRx(ch);
            (void)osThreadYield();
        }

    };
    /* Initial pass consumes frames accepted before this worker was registered. */
    SRNOS_KernelRunCpp(step,wait,0,false);
}
