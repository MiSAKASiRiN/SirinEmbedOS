#ifndef SRNOS_IO_I2C_H
#define SRNOS_IO_I2C_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "SirinEmbedOS/Kernel/Thread.h"
#if defined(SRNOS_MEMORY_SECTIONS) && SRNOS_MEMORY_SECTIONS
#include "SirinEmbedOS/Core/Memory/MemorySections.h"
#define SRNOS_I2C_DMA_SEC SRNOS_BDMA_BUF_BSS
#else
#define SRNOS_I2C_DMA_SEC
#endif
#ifndef SRNOS_I2C_DMA_STAGING_SIZE
#define SRNOS_I2C_DMA_STAGING_SIZE 256U
#endif
/* Limit a non-preemptible transaction on the fixed I2C4 bus.
 * Device drivers must split longer operations respecting device semantics. */
#define SRNOS_I2C_MAX_TRANSFER_BYTES 64U
#define SRNOS_I2C_PERIODIC_WAIT_MS 30U
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SRNOS_I2C_OK = 0, SRNOS_I2C_ERROR, SRNOS_I2C_BUSY, SRNOS_I2C_INVALID_PARAM,
    SRNOS_I2C_TIMEOUT, SRNOS_I2C_CANCELLED
} SRNOS_I2CStatus;
typedef enum { SRNOS_I2C_ADDRESS_8BIT=1, SRNOS_I2C_ADDRESS_16BIT=2 } SRNOS_I2CAddressWidth;
typedef enum { SRNOS_I2C_BUS_4 = 0, SRNOS_I2C_BUS_MAX } SRNOS_I2CBusId;
struct SRNOS_I2CBus;
/* Descriptors have static/operation lifetime. Initialize once before use.
 * The owner must serialize submit/wait on each handle across tasks.
 * One outstanding request per physical address, even across different handles.
 * pending covers admission/copy + queued + active work, cleared after all RX
 * accesses finish.
 * With a callback, user context/handle must live until the callback returns. */
typedef struct {
    struct SRNOS_I2CBus *bus;
    uint16_t addr_7bit;
    uint32_t max_speed_hz;
    volatile uint8_t pending;
    volatile SRNOS_I2CStatus completed_status;
    uint32_t flags;
} SRNOS_I2CDevice;
typedef void (*SRNOS_I2CCallback)(SRNOS_I2CDevice *, void *, SRNOS_I2CStatus);

typedef struct {
    uint32_t accepted, completed, errors, timeouts, cancelled, queue_full;
    uint32_t max_queue_us, max_transfer_us;
} SRNOS_I2CClassStatistics;
typedef struct {
    uint32_t state, hal_state, hal_error, lock_held;
    uintptr_t lock_owner, current_dev;
    uint32_t current_len, last_status, last_tick, recover_count;
    uint32_t cb_queued, cb_queue_overflow; /* No separate callback queue now. */
    uint32_t queued, high_water, quarantined;
    SRNOS_I2CClassStatistics periodic, background;
} SRNOS_I2CSnapshot;
#define SRNOS_I2C_RECOVER_REASON_TIMEOUT 1U
#define SRNOS_I2C_RECOVER_REASON_USER 255U

/* Initialize a caller-owned descriptor before submitting its first request.
 * Hardware/RTOS startup belongs to the SDK and has no public entry point. */
SRNOS_I2CStatus SRNOS_I2CDeviceInit(SRNOS_I2CDevice *, SRNOS_I2CBusId, uint16_t addr_7bit);

/* Task-only enqueue, scheduler running, IRQs enabled. OK means accepted, NOT
 * completed. BUSY means same address already pending or its slots exhausted;
 * another device merely using the bus does not reject this request.
 * Fixed periodic addresses 0x1E/0x76 each have a reserved slot. Two background
 * slots cannot consume them. FIFO within class; at most four periodic starts
 * before one waiting background request. No preemption of an active transfer.
 * Queue deadlines: periodic 20 ms, background 100 ms. Active deadline 10 ms.
 * Timeout is distinct from BUSY/NACK/error. Callbacks execute on the dispatcher,
 * must be short and never call wait_complete/cancel/device_ready/recover.
 * TX is copied at enqueue. RX buffer lives until completion/cancel acknowledgement.
 * Length is 1..64 bytes. Memory addresses are 8/16-bit as explicitly specified;
 * no implicit auto-increment/splitting of unknown register protocols. */
SRNOS_I2CStatus SRNOS_I2CMasterTransmit(SRNOS_I2CDevice *, const uint8_t *, size_t, SRNOS_I2CCallback, void *);
SRNOS_I2CStatus SRNOS_I2CMasterReceive(SRNOS_I2CDevice *, uint8_t *, size_t, SRNOS_I2CCallback, void *);
SRNOS_I2CStatus SRNOS_I2CMemRead(SRNOS_I2CDevice *, uint16_t, uint16_t, uint8_t *, size_t, SRNOS_I2CCallback, void *);
SRNOS_I2CStatus SRNOS_I2CMemWrite(SRNOS_I2CDevice *, uint16_t, uint16_t, const uint8_t *, size_t, SRNOS_I2CCallback, void *);
/* Wait using a completion semaphore, not tick polling. One waiter per device.
 * SRNOS_NO_WAIT only queries: BUSY means the request/callback still owns its
 * buffers; it neither cancels nor installs a waiter. SRNOS_WAIT_FOREVER removes
 * the caller wait deadline but preserves queue/active hardware limits.
 * Finite positive timeout_ms <= INT32_MAX starts at this wait and includes
 * remaining queueing. Expiry cancels only this request and waits for dispatcher
 * acknowledgement, releasing RX ownership before returning TIMEOUT.
 * Cleanup may add a bounded abort (5 ms + scheduling/callback overhead).
 * Already completed requests return the retained device result. */
SRNOS_I2CStatus SRNOS_I2CWaitComplete(SRNOS_I2CDevice *, uint32_t timeout_ms);
/* Blocking ACK probe is queued too. Worker performs ONE <=1 ms HAL probe at
 * a time; trials are separate transactions, bounded by caller timeout_ms.
 * SRNOS_NO_WAIT returns TIMEOUT without starting a probe. Finite positive
 * waits are <= INT32_MAX; SRNOS_WAIT_FOREVER removes the overall deadline,
 * while trials and each transaction's hardware limits remain bounded.
 * No EEPROM write-cycle loop runs in the dispatcher. */
SRNOS_I2CStatus SRNOS_I2CDeviceReady(SRNOS_I2CDevice *, uint32_t trials, uint32_t timeout_ms);
/* Task-only cancel, waits until matching admission/copy, request and callback
 * no longer use buffers. Queued requests are removed without HAL abort. Active cancellation
 * confirms HAL/BDMA stop; failure quarantines bus and rejects future transfers.
 * A completed request never aborts a subsequent request of another device. */
SRNOS_I2CStatus SRNOS_I2CCancelDevice(SRNOS_I2CDevice *, uint32_t reason);
/* Cancels only the active request observed at entry, preserving admission/queued work
 * when abort succeeds. A quarantined bus requires system restart. */
SRNOS_I2CStatus SRNOS_I2CRecoverBus(SRNOS_I2CBusId, uint32_t reason);
SRNOS_I2CStatus SRNOS_I2CIsBusBusy(const SRNOS_I2CDevice *);
SRNOS_I2CStatus SRNOS_I2CGetSnapshot(SRNOS_I2CBusId, SRNOS_I2CSnapshot *);
/* Raw bus acquire/release removed: all hardware access belongs to dispatcher. */
#ifdef __cplusplus
}
#endif
#endif
