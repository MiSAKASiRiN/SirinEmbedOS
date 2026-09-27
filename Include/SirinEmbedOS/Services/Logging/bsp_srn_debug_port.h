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
    SRN_DEBUG_PORT_WRITE_OK = 0,
    SRN_DEBUG_PORT_WRITE_INVALID,
    SRN_DEBUG_PORT_WRITE_TOO_LONG,
    SRN_DEBUG_PORT_WRITE_FULL,
    SRN_DEBUG_PORT_WRITE_NOT_CONFIGURED,
} srn_debug_port_write_status_t;

typedef enum {
    SRN_DEBUG_PORT_RX_OK = 0,
    SRN_DEBUG_PORT_RX_INVALID,
    SRN_DEBUG_PORT_RX_BUSY,
    SRN_DEBUG_PORT_RX_HAL_ERROR,
} srn_debug_port_rx_status_t;

typedef srn_debug_port_write_status_t (*srn_debug_port_write_sink_fn)(
    void *user,
    const void *data,
    size_t len,
    bool high_priority);

/*
 * USB CDC debug-port transport init.
 *
 * Resets TX/RX software buffers. RX is not accepted until
 * SRN_DebugPort_StartRx() opts in.
 */
void SRN_DebugPort_Init(void);

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
 * If the core clock differs from 275 MHz, adjust SRN_SWO_PRESCALER before
 * calling or pass trace_hz / target_hz.
 */
void SRN_SWO_Init(void);

/* Register the task responsible for kicking queued TX DMA chunks. */
void SRN_DebugPort_RegisterTxServiceTask(TaskHandle_t task);

/* Bind the project-owned scheduler used for protocol (high) and log (normal). */
void SRN_DebugPort_RegisterWriteSink(srn_debug_port_write_sink_fn sink,
                                    void *user);

/* Try to submit one queued TX chunk to USB CDC from task context. */
void SRN_DebugPort_ServiceTxOnce(void);

/* Zero-copy physical transport API. data must remain valid until completion. */
srn_debug_port_write_status_t SRN_DebugPort_TryTransmit(const uint8_t *data,
                                                        size_t len);
bool SRN_DebugPort_TakeTxComplete(bool *success);
bool SRN_DebugPort_IsConfigured(void);
bool SRN_DebugPort_TxBusy(void);
uint32_t SRN_DebugPort_GetRxOverflowBytes(void);
uint32_t SRN_DebugPort_GetSessionGeneration(void);

/* Thread-safe whole-buffer TX write. Space shortage rejects the whole block. */
srn_debug_port_write_status_t SRN_DebugPort_WriteExact(const void *data,
                                                       size_t len);

/*
 * Thread-safe whole-buffer TX write for COBS/protocol replies.
 *
 * High-priority data shares USB CDC with normal logs but is drained before the
 * normal TX ring. Already-started USB IN transfers are not preempted.
 */
srn_debug_port_write_status_t SRN_DebugPort_WriteHighPrioExact(const void *data,
                                                               size_t len);

/* Convenience wrappers: success returns len/char, failure returns 0/-1. */
size_t SRN_DebugPort_Write(const void *data, size_t len, TickType_t timeout);
size_t SRN_DebugPort_WriteStr(const char *s, TickType_t timeout);
int SRN_DebugPort_PutChar(int ch, TickType_t timeout);

/*
 * Opt-in raw RX channel.
 *
 * If notify_task is non-NULL, each RX event notifies it. When notify_bits is
 * nonzero eSetBits is used; when it is zero vTaskNotifyGiveFromISR is used.
 */
srn_debug_port_rx_status_t SRN_DebugPort_StartRx(TaskHandle_t notify_task,
                                                 uint32_t notify_bits);
void SRN_DebugPort_StopRx(void);
size_t SRN_DebugPort_RxAvailable(void);
size_t SRN_DebugPort_Read(uint8_t *out, size_t max_len);

/* Called from USB CDC ACM USER CODE callbacks. */
void SRN_DebugPort_OnUsbReceive(const uint8_t *data, uint32_t len);
void SRN_DebugPort_OnUsbTransmitComplete(void);
void SRN_DebugPort_OnUsbDisconnect(void);
void SRN_DebugPort_OnUsbControlLineState(uint16_t state);

#ifdef __cplusplus
}
#endif
