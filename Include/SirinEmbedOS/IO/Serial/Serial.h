#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Values describe this board's actual connectors; NONE means unbound. */
typedef enum { SRNOS_SERIAL_NONE=0, SRNOS_SERIAL_USART1, SRNOS_SERIAL_USART6,
 SRNOS_SERIAL_UART7, SRNOS_SERIAL_UART8, SRNOS_SERIAL_UART9, SRNOS_SERIAL_PORT_COUNT } SRNOS_SerialPortId;
typedef enum { SRNOS_SERIAL_PARITY_NONE=0, SRNOS_SERIAL_PARITY_EVEN, SRNOS_SERIAL_PARITY_ODD } SRNOS_SerialParity;
typedef enum { SRNOS_SERIAL_DUPLEX=0, SRNOS_SERIAL_RS485 } SRNOS_SerialMode;
typedef enum { SRNOS_SERIAL_OK=0, SRNOS_SERIAL_ARGUMENT=-1, SRNOS_SERIAL_CONFLICT=-2,
 SRNOS_SERIAL_FROZEN=-3, SRNOS_SERIAL_STATE=-4, SRNOS_SERIAL_TIMEOUT=-5,
 SRNOS_SERIAL_IO=-6, SRNOS_SERIAL_OVERFLOW=-7 } SRNOS_SerialStatus;
typedef struct {
 SRNOS_SerialPortId port;
 uint32_t baud_rate;
 uint8_t data_bits; /* payload bits: 7 or 8, excluding parity */
 uint8_t stop_bits; /* 1 or 2 */
 SRNOS_SerialParity parity;
 SRNOS_SerialMode mode; /* UART7 must be RS485; no unverified single-wire mode */
 bool rx_enabled, tx_enabled, rx_inverted;
} SRNOS_SerialConfig;
typedef struct SRNOS_SerialChannel SRNOS_SerialChannel;
typedef struct { uint32_t rx_bytes, rx_overflow, tx_accepted, tx_completed,
 tx_errors, hardware_errors, recoveries;
 bool hardware_ready; SRNOS_SerialStatus last_error; } SRNOS_SerialStats;
#define SRNOS_SERIAL_MAX_WRITE 512u
/* Pure validation, then one-shot configure before scheduler start. Zero ports is valid. */
SRNOS_SerialStatus SRNOS_SerialValidate(const SRNOS_SerialConfig *, size_t);
SRNOS_SerialStatus SRNOS_SerialConfigure(const SRNOS_SerialConfig *, size_t);
/* Create synchronization objects/one shared worker after osKernelInitialize. */
SRNOS_SerialStatus SRNOS_SerialStart(void);
/* Initialization-only: returns an unclaimed channel. Returns NULL after a
 * service has claimed it or after startup; retain your successfully claimed
 * handle for runtime I/O. Every configured port must have one owner at Start. */
SRNOS_SerialChannel *SRNOS_SerialGetChannel(SRNOS_SerialPortId);
const SRNOS_SerialConfig *SRNOS_SerialGetConfig(const SRNOS_SerialChannel *);
/* One protocol/service owner per channel, claimed during configuration only. */
SRNOS_SerialStatus SRNOS_SerialClaim(SRNOS_SerialChannel *, const void *owner);
/* All I/O is task-only. Timeouts in ms; 0 polls, UINT32_MAX waits indefinitely.
 * Read copies up to capacity; Write copies the whole packet or accepts nothing.
 * Flush waits for UART TC, not DMA completion; a prior TX error is reported. */
SRNOS_SerialStatus SRNOS_SerialRead(SRNOS_SerialChannel *, void *, size_t, size_t *, uint32_t);
SRNOS_SerialStatus SRNOS_SerialWrite(SRNOS_SerialChannel *, const void *, size_t, uint32_t);
SRNOS_SerialStatus SRNOS_SerialFlush(SRNOS_SerialChannel *, uint32_t);
SRNOS_SerialStatus SRNOS_SerialDiscardRx(SRNOS_SerialChannel *);
/* Matcher runs under the transaction lock. Return frame length when complete,
 * zero for more bytes, -1 for malformed data. Do not call channel APIs in it. */
typedef int (*SRNOS_SerialMatch)(const uint8_t *, size_t, void *);
typedef struct {
 const void *tx_data; size_t tx_size;
 void *rx_data; size_t rx_capacity;
 uint32_t queue_timeout_ms, response_timeout_ms, silence_us;
 SRNOS_SerialMatch match; void *match_context;
} SRNOS_SerialTransaction;
/* Serializes the entire request/response; rx_capacity=0 is a broadcast/write.
 * A timeout before TX completion schedules abort/drain; new sends return STATE
 * until cleanup completes. A timeout cannot undo bytes already on the wire.
 * No matcher: collect until silence (minimum 2 ms scheduling granularity).
 * silence_us also enforces a minimum bus-idle interval before transmission. */
SRNOS_SerialStatus SRNOS_SerialTransceive(SRNOS_SerialChannel *, const SRNOS_SerialTransaction *, size_t *);
SRNOS_SerialStatus SRNOS_SerialGetStats(SRNOS_SerialChannel *, SRNOS_SerialStats *);
#ifdef __cplusplus
}
#endif
