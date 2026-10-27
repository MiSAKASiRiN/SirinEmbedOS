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

/*
 * USB CDC debug-port transport init.
 *
 * Resets TX/RX software buffers. RX is not accepted until
 * SRNOS_DebugPortStartRx() opts in.
 */
void SRNOS_DebugPortInit(void);

/* Notify the SirinLink service task about USB TX completion/session events. */
void SRNOS_DebugPortRegisterTxServiceTask(TaskHandle_t task);

/* Zero-copy physical transport API. data must remain valid until completion. */
SRNOS_DebugPortWriteStatus SRNOS_DebugPortTryTransmit(const uint8_t *data,
                                                        size_t len);
bool SRNOS_DebugPortTakeTxComplete(bool *success);
bool SRNOS_DebugPortIsConfigured(void);
bool SRNOS_DebugPortTxBusy(void);
uint32_t SRNOS_DebugPortGetRxOverflowBytes(void);
uint32_t SRNOS_DebugPortGetSessionGeneration(void);

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
