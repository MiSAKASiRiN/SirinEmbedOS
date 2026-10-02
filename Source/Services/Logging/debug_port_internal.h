#pragma once

#include "stm32h7xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SRNOS_DEBUG_PORT_WRITE_OK = 0,
    SRNOS_DEBUG_PORT_WRITE_INVALID,
    SRNOS_DEBUG_PORT_WRITE_TOO_LONG,
    SRNOS_DEBUG_PORT_WRITE_FULL,
    SRNOS_DEBUG_PORT_WRITE_NOT_CONFIGURED,
} SRNOS_DebugPortWriteStatus;

typedef enum {
    SRNOS_DEBUG_PORT_RX_OK = 0,
    SRNOS_DEBUG_PORT_RX_INVALID,
    SRNOS_DEBUG_PORT_RX_BUSY,
    SRNOS_DEBUG_PORT_RX_HAL_ERROR,
} SRNOS_DebugPortRxStatus;

typedef SRNOS_DebugPortWriteStatus (*SRNOS_DebugPortWriteSinkFn)(
    void *user,
    const void *data,
    size_t len,
    bool high_priority);

/*
 * USB CDC debug-port transport init.
 *
 * Resets TX/RX software buffers. RX is not accepted until
 * SRNOS_DebugPortStartRx() opts in.
 */
void SRNOS_DebugPortInit(void);

/* Register the task responsible for kicking queued USB CDC TX chunks. */

/*
 * SWO/ITM hardware init for J-Link CDC (Serial Wire Viewer).
 *
 * Call once before any SWO output. Configures:
 *   - TPIU async prescaler → 2 MHz SWO from 275 MHz trace clock
 *   - TPIU NRZ (async UART-like) protocol
 *   - ITM unlock, enable, stimulus port 0
 *
 * J-Link side: open SWV, set SWO speed = 2000 kHz, stimulus port 0.
 * If the core clock differs from 275 MHz, adjust SRNOS_SWO_PRESCALER before
 * calling or pass trace_hz / target_hz.
 */
void SRNOS_SwoInit(void);

/* Register the task responsible for kicking queued TX DMA chunks. */
void SRNOS_DebugPortRegisterTxServiceTask(TaskHandle_t task);

/* Bind the project-owned scheduler used for protocol (high) and log (normal). */
void SRNOS_DebugPortRegisterWriteSink(SRNOS_DebugPortWriteSinkFn sink,
                                    void *user);

/* Try to submit one queued TX chunk to USB CDC from task context. */
void SRNOS_DebugPortServiceTxOnce(void);

/* Zero-copy physical transport API. data must remain valid until completion. */
SRNOS_DebugPortWriteStatus SRNOS_DebugPortTryTransmit(const uint8_t *data,
                                                        size_t len);
bool SRNOS_DebugPortTakeTxComplete(bool *success);
bool SRNOS_DebugPortIsConfigured(void);
bool SRNOS_DebugPortTxBusy(void);
uint32_t SRNOS_DebugPortGetRxOverflowBytes(void);
uint32_t SRNOS_DebugPortGetSessionGeneration(void);

/* Thread-safe whole-buffer TX write. Space shortage rejects the whole block. */
SRNOS_DebugPortWriteStatus SRNOS_DebugPortWriteExact(const void *data,
                                                       size_t len);

/*
 * Thread-safe whole-buffer TX write for COBS/protocol replies.
 *
 * High-priority data shares USB CDC with normal logs but is drained before the
 * normal TX ring. Already-started USB IN transfers are not preempted.
 */
SRNOS_DebugPortWriteStatus SRNOS_DebugPortWriteHighPrioExact(const void *data,
                                                               size_t len);

/* Convenience wrappers: success returns len/char, failure returns 0/-1. */
size_t SRNOS_DebugPortWrite(const void *data, size_t len, TickType_t timeout);
size_t SRNOS_DebugPortWriteStr(const char *s, TickType_t timeout);
int SRNOS_DebugPortPutChar(int ch, TickType_t timeout);

/*
 * Opt-in raw RX channel.
 *
 * If notify_task is non-NULL, each RX event notifies it. When notify_bits is
 * nonzero eSetBits is used; when it is zero vTaskNotifyGiveFromISR is used.
 */
SRNOS_DebugPortRxStatus SRNOS_DebugPortStartRx(TaskHandle_t notify_task,
                                                 uint32_t notify_bits);
void SRNOS_DebugPortStopRx(void);
size_t SRNOS_DebugPortRxAvailable(void);
size_t SRNOS_DebugPortRead(uint8_t *out, size_t max_len);

/* Called from USB CDC ACM USER CODE callbacks. */
void SRNOS_DebugPortOnUsbReceive(const uint8_t *data, uint32_t len);
void SRNOS_DebugPortOnUsbTransmitComplete(void);
void SRNOS_DebugPortOnUsbDisconnect(void);
void SRNOS_DebugPortOnUsbControlLineState(uint16_t state);

#ifdef __cplusplus
}
#endif
