#ifndef SRNOS_NATIVE_CAN_BACKEND_INTERNAL_H
#define SRNOS_NATIVE_CAN_BACKEND_INTERNAL_H

#include "can_router_core_internal.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SRNOS_NATIVE_CAN_EVT_RX (1u << 0)
#define SRNOS_NATIVE_CAN_EVT_TX (1u << 1)

typedef struct {
    TaskHandle_t rx_thread;
    TaskHandle_t tx_thread;
} SRNOS_NativeCANTaskCtx;

void SRNOS_NativeCANRxRouterWorker(void *argument);
void SRNOS_NativeCANTxRouterWorker(void *argument);
SRNOS_NativeCANTaskCtx *SRNOS_NativeCANTaskCtxOf(SRNOS_CANChannel ch);
void SRNOS_NativeCANBackendEnsureBound(SRNOS_CANChannel ch);
void SRNOS_NativeCANBackendSignalRx(SRNOS_CANChannel ch);
void SRNOS_NativeCANBackendSignalTx(SRNOS_CANChannel ch);
uint32_t SRNOS_NativeCANBackendNowMs(void);

#ifdef __cplusplus
}
#endif

#endif /* SRNOS_NATIVE_CAN_BACKEND_INTERNAL_H */
