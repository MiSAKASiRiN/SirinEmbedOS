#ifndef SRNOS_DEVICES_EEPROM_H
#define SRNOS_DEVICES_EEPROM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed board FT24C128A, I2C4, A2..A0=0, WP grounded. Byte addresses. */
#define SRNOS_EEPROM_SIZE_BYTES 16384U
#define SRNOS_EEPROM_PAGE_BYTES 64U
#define SRNOS_EEPROM_ADDRESS_7BIT 0x50U

typedef enum {
    SRNOS_EEPROM_OK = 0,
    SRNOS_EEPROM_IN_PROGRESS,
    SRNOS_EEPROM_BUSY,
    SRNOS_EEPROM_INVALID_ARGUMENT,
    SRNOS_EEPROM_CONTEXT_ERROR,
    SRNOS_EEPROM_IO_ERROR,
    SRNOS_EEPROM_TIMEOUT,
    SRNOS_EEPROM_VERIFY_ERROR,
    SRNOS_EEPROM_CANCELLED
} SRNOS_EEPROMStatus;

/* All operations require a running scheduler, task context and enabled IRQs.
 * One outstanding operation for the fixed chip. Start returns OK on acceptance;
 * it does no bus I/O. The accepting task must Poll until terminal status, or
 * Cancel. No worker, dynamic allocation, callback or implicit background work.
 * timeout_ms (1..INT32_MAX) bounds EACH bus acquisition/transaction; it is not
 * an entire multi-page operation deadline. Poll cadence adds caller latency.
 * Buffers remain owned by caller and valid until terminal Poll/Cancel; TX must
 * remain unchanged. RX may contain a completed prefix after error/cancellation.
 * Zero length and ranges crossing 0x4000 are rejected before any bus access.
 */
SRNOS_EEPROMStatus SRNOS_EEPROMReadAsync(uint16_t address, void *data,
                                        size_t length, uint32_t timeout_ms);
SRNOS_EEPROMStatus SRNOS_EEPROMWriteAsync(uint16_t address, const void *data,
                                         size_t length, uint32_t timeout_ms);
/* Advances at most one transaction/state per call; IN_PROGRESS while active.
 * Uses BSP DMA setup and interrupt-driven address/data transfer. No write-cycle
 * delay loops. Timeout recovery uses existing BSP abort, bounded to 5 ms plus
 * scheduling/HAL overhead. Not a hard-real-time/ISR API.
 * Write OK means every page's tWR elapsed AND written bytes read back equal.
 * Poll/Cancel from another task returns BUSY without altering the operation.
 */
SRNOS_EEPROMStatus SRNOS_EEPROMPoll(void);
SRNOS_EEPROMStatus SRNOS_EEPROMCancel(void);
bool SRNOS_EEPROMIsBusy(void);

/* Blocking wrappers over exactly the same state machine. Yield one RTOS tick
 * between polls. Partial writes cannot be rolled back by cancellation/errors.
 */
SRNOS_EEPROMStatus SRNOS_EEPROMRead(uint16_t address, void *data,
                                   size_t length, uint32_t timeout_ms);
SRNOS_EEPROMStatus SRNOS_EEPROMWrite(uint16_t address, const void *data,
                                    size_t length, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
#endif
