#include "SirinEmbedOS/Kernel/Thread.h"
#include "thread_internal.h"
#include "native_can_backend_internal.h"

#include "IO/CAN/Native/fdcan_internal.h"
#include "../can_controller_internal.h"
#include "../FD/can_fd_internal.h"
#include "cmsis_os2.h"
#include "../Native/fdcan_hal.h"

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
    return SRNOS_TimeMs();
}

static bool SRNOS_NativeCANBackendStart(SRNOS_CANChannel ch)
{
    return SRNOS_FDCANStart(ch);
}

static uint8_t SRNOS_NativeCANBackendGetTxFree(SRNOS_CANChannel ch)
{
    FDCAN_GlobalTypeDef *inst = fdcan_inst_of(ch);
    if (!inst || !SRNOS_FDCANIsReady(ch)) {
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

/* Notification index 0 belongs to this SDK worker; managed-thread control
 * uses index 1. ISR delivery wakes the worker directly, without TimerTask. */
static void signal(TaskHandle_t *slot, uint32_t bits)
{
    TaskHandle_t thread=__atomic_load_n(slot,__ATOMIC_ACQUIRE);
    if(!thread) return; /* Worker always drains once after publishing its handle. */
    if(__get_IPSR()) {
        BaseType_t higher=pdFALSE;
        (void)xTaskNotifyFromISR(thread,bits,eSetBits,&higher);
        portYIELD_FROM_ISR(higher);
    } else (void)xTaskNotify(thread,bits,eSetBits);
}
extern "C" void SRNOS_NativeCANBackendSignalRx(SRNOS_CANChannel ch)
{
    auto *ctx=SRNOS_NativeCANTaskCtxOf(ch);
    if(ctx) signal(&ctx->rx_thread,SRNOS_NATIVE_CAN_EVT_RX);
}
extern "C" void SRNOS_NativeCANBackendSignalTx(SRNOS_CANChannel ch)
{
    auto *ctx=SRNOS_NativeCANTaskCtxOf(ch);
    if(ctx) signal(&ctx->tx_thread,SRNOS_NATIVE_CAN_EVT_TX);
}

static const SRNOS_CANRouterBackendOps s_backend_ops = {
    .start = SRNOS_NativeCANBackendStart,
    .get_tx_free = SRNOS_NativeCANBackendGetTxFree,
    .try_push = SRNOS_NativeCANBackendTryPush,
    .signal_tx = SRNOS_NativeCANBackendSignalTx,
    .now_ms = SRNOS_NativeCANBackendNowMs,
    .retry_wait_ms = nullptr,
};

static const SRNOS_CANFDBackendOps s_fd_backend_ops = {
    .get_tx_free = SRNOS_NativeCANBackendGetTxFree,
    .try_push = SRNOS_FDCANTxPushFD,
    .signal_tx = SRNOS_NativeCANBackendSignalTx,
    .now_ms = SRNOS_NativeCANBackendNowMs,
    .retry_wait_ms = nullptr,
};

extern "C" void SRNOS_NativeCANBackendEnsureBound(SRNOS_CANChannel ch)
{
    if (SRNOS_CANControllerIsFD(ch)) SRNOS_CANFDCoreSetBackend(ch, &s_fd_backend_ops);
    else SRNOS_CANRouterCoreSetBackend(ch, &s_backend_ops);
}

extern "C" void SRNOS_FDCANOnTxEvent(uint8_t bus)
{
    if (SRNOS_CANControllerIsFD(bus)) SRNOS_CANFDCoreNoteTxComplete(bus);
    else SRNOS_CANRouterCoreNoteTxComplete(bus);
}

extern "C" void SRNOS_FDCANOnTxReady(uint8_t bus)
{
    SRNOS_NativeCANBackendSignalTx(bus);
}
extern "C" void SRNOS_FDCANOnBusoff(uint8_t bus)
{
    SRNOS_NativeCANBackendSignalTx(bus);
}
static TickType_t wait_ticks(uint32_t ms)
{
    if(ms==UINT32_MAX) return portMAX_DELAY;
    return (TickType_t)(((uint64_t)ms*configTICK_RATE_HZ+999u)/1000u);
}
extern "C" void SRNOS_NativeCANTxRouterWorker(void *argument)
{
    const SRNOS_CANChannel ch=(SRNOS_CANChannel)(uintptr_t)argument;
    auto *ctx=SRNOS_NativeCANTaskCtxOf(ch);
    if(!ctx) return;
    SRNOS_NativeCANBackendEnsureBound(ch);
    __atomic_store_n(&ctx->tx_thread,xTaskGetCurrentTaskHandle(),__ATOMIC_RELEASE);
    (void)SRNOS_FDCANStart(ch);
    auto wait=[&]()->bool {
        uint32_t now=SRNOS_NativeCANBackendNowMs();
        uint32_t delay=SRNOS_CANControllerIsFD(ch) ? SRNOS_CANFDCoreNextTxWait(ch,now) :
                                                  SRNOS_CANRouterCoreNextTxWait(ch,now);
        uint32_t recovery=SRNOS_FDCANNextServiceWait(ch,now);
        if(recovery<delay) delay=recovery;
        uint32_t flags=0;
        (void)xTaskNotifyWait(0,UINT32_MAX,&flags,wait_ticks(delay));
        return true;
    };
    auto step=[&](){
        const uint32_t now=SRNOS_NativeCANBackendNowMs();
        (void)SRNOS_FDCANService(ch,now);
        /* Expiry and adapter ticks continue during recovery; the backend
         * rejects hardware submissions until recovery is confirmed. */
        if (SRNOS_CANControllerIsFD(ch)) SRNOS_CANFDCoreDrainTx(ch,now);
        else SRNOS_CANRouterCoreDrainTx(ch,now);
    };
    SRNOS_KernelRunCpp(step,wait,0,false);
}
