//
// Created by sirin on 2025/11/29.
//

#ifndef SRNOS_SPI_H
#define SRNOS_SPI_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "SirinEmbedOS/Kernel/Thread.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Cache / DMA buffer compatibility
 * ============================================================
 *
 * Default transfer APIs copy caller buffers through internal non-cacheable
 * staging buffers, so callers do not need a special memory section. The SDK
 * uses SRNOS_SPI_DMA_SEC for its own staging storage.
 */
#if defined(SRNOS_MEMORY_SECTIONS) && SRNOS_MEMORY_SECTIONS
#include "SirinEmbedOS/Core/Memory/MemorySections.h"
#define SRNOS_SPI_DMA_SEC SRNOS_DMA_NC_BUF_BSS
#else
#define SRNOS_SPI_DMA_SEC
#endif

#ifndef SRNOS_SPI_DMA_STAGING_SIZE
#define SRNOS_SPI_DMA_STAGING_SIZE (512u)
#endif

/* ============================================================
 * ENUMS
 * ============================================================ */
typedef enum
{
    SRNOS_SPI_OK = 0,
    SRNOS_SPI_BUSY,
    SRNOS_SPI_ERROR,
    SRNOS_SPI_INVALID_PARAM,
    SRNOS_SPI_TIMEOUT,
    SRNOS_SPI_CANCELLED
} SRNOS_SPIStatus;

typedef enum
{
    SRNOS_SPI_BUS_4 = 0,
    SRNOS_SPI_BUS_MAX
} SRNOS_SPIBusId;

struct SRNOS_SPIBus;
typedef struct {
    uint32_t accepted, completed, queue_full, queue_expired, errors, timeouts, recoveries;
    uint32_t max_queue_us, max_transfer_us;
    /* <=25, <=100, <=250, <=500, <=1000, >1000 us. */
    uint32_t queue_us[6], transfer_us[6];
} SRNOS_SPIStatistics;

/* ============================================================
 * SPI Device Structure
 * ============================================================ */
typedef struct SRNOS_SPIDevice SRNOS_SPIDevice;

/* Callback type */
typedef void (*SRNOS_SPICallback)(SRNOS_SPIDevice *dev, void *user_ctx, SRNOS_SPIStatus status);

/* lock_last_fail codes */
#define SRNOS_SPI_LOCK_FAIL_NONE              0u
#define SRNOS_SPI_LOCK_FAIL_IN_ISR            1u
#define SRNOS_SPI_LOCK_FAIL_KERNEL_NOT_RUNNING 2u
#define SRNOS_SPI_LOCK_FAIL_ALREADY_HELD      3u
#define SRNOS_SPI_LOCK_FAIL_SEM_NULL          4u
#define SRNOS_SPI_LOCK_FAIL_TIMEOUT           5u
#define SRNOS_SPI_LOCK_FAIL_OS_RESOURCE       6u
#define SRNOS_SPI_LOCK_FAIL_OS_ERROR          7u

typedef struct
{
    uint32_t  state;
    uint32_t  hal_state;
    uint32_t  hal_error;
    uint32_t  lock_held;
    uintptr_t lock_owner;
    uintptr_t current_dev;
    uint32_t  current_len;
    uint32_t  last_status;
    uint32_t  last_tick;
    uint32_t  recover_count;
    uint32_t  cb_queued;
    uint32_t  cb_queue_overflow;
    uint32_t  launch_guard;
    uint32_t  pending_status;
    uint32_t  early_complete;
    uint32_t  deferred_publish;
    uint32_t  spurious_complete;
    uint32_t  pending_overwrite;
    uint32_t  start_conflict;
    uint32_t  last_hal_ret;
} SRNOS_SPISnapshot;

#define SRNOS_SPI_RECOVER_REASON_TIMEOUT       1u
#define SRNOS_SPI_RECOVER_REASON_BMI088_STALE  2u
#define SRNOS_SPI_RECOVER_REASON_USER          255u

/* ============================================================
 * PUBLIC API
 * ============================================================ */

/* Task context only, after scheduler starts. Device descriptors are opaque,
 * owned and bound by SDK board/device code; applications do not supply HAL/CS.
 * Eight static slots (4 IMU / 4 normal), TX copied on admission, maximum
 * 512 bytes including command. Copying occurs with interrupts enabled.
 * Async OK means accepted, RX lives until callback or cancel returns OK.
 * Callbacks run on High6 dispatcher and MUST NOT block. Finite timeout_ms is
 * <= INT32_MAX; sync covers admission/copy, queueing and completion. All DMA
 * requests need deferred dispatch, so SRNOS_NO_WAIT returns TIMEOUT without
 * submitting, blocking or taking buffer ownership. SRNOS_WAIT_FOREVER removes
 * the caller's wait deadline; internal IMU/normal queue limits remain 1/5 ms,
 * and the active hardware limit remains 5 ms. Async calls never wait for
 * completion: a positive finite timeout sets the normal queue deadline;
 * TransferAsync/TransferDMA explicitly use the default 5 ms queue limit.
 * Expiry then waits for cancellation acknowledgement before returning; this
 * cleanup can extend the elapsed call time. RX is never released while a
 * request may still copy into it. No direct DMA or external bus lock entry. */
SRNOS_SPIStatus SRNOS_SPICancel(SRNOS_SPIDevice *dev);
void SRNOS_SPIGetStatistics(const SRNOS_SPIDevice *, SRNOS_SPIStatistics *);





SRNOS_SPIStatus SRNOS_SPITransferDMA(SRNOS_SPIDevice *dev,
                                      const uint8_t *tx_buf,
                                      uint8_t *rx_buf,
                                      size_t len,
                                      SRNOS_SPICallback cb,
                                      void *user_ctx);



SRNOS_SPIStatus SRNOS_SPITransferDMABlocking(SRNOS_SPIDevice *dev,
                                               const uint8_t *tx_buf,
                                               uint8_t *rx_buf,
                                               size_t len,
                                               uint32_t timeout_ms);

SRNOS_SPIStatus SRNOS_SPITransferEx(SRNOS_SPIDevice *dev,
                                     const uint8_t *tx_buf,
                                     uint8_t *rx_buf,
                                     size_t len,
                                     SRNOS_SPICallback cb,
                                     void *user_ctx,
                                     uint8_t blocking,
                                     uint32_t timeout_ms);



static inline SRNOS_SPIStatus SRNOS_SPITransferBlocking(SRNOS_SPIDevice *dev,
                                                         const uint8_t *tx,
                                                         uint8_t *rx,
                                                         size_t len,
                                                         uint32_t timeout)
{
    return SRNOS_SPITransferEx(dev, tx, rx, len, NULL, NULL, 1, timeout);
}

static inline SRNOS_SPIStatus SRNOS_SPITransferAsync(SRNOS_SPIDevice *dev,
                                                      const uint8_t *tx,
                                                      uint8_t *rx,
                                                      size_t len,
                                                      SRNOS_SPICallback cb,
                                                      void *ctx)
{
    return SRNOS_SPITransferEx(dev, tx, rx, len, cb, ctx, 0, 5);
}

static inline SRNOS_SPIStatus SRNOS_SPITransferAsyncWait(SRNOS_SPIDevice *dev,
                                                           const uint8_t *tx,
                                                           uint8_t *rx,
                                                           size_t len,
                                                           SRNOS_SPICallback cb,
                                                           void *ctx,
                                                           uint32_t timeout_ms)
{
    return SRNOS_SPITransferEx(dev, tx, rx, len, cb, ctx, 0, timeout_ms);
}





SRNOS_SPIStatus SRNOS_SPIIsBusBusy(const SRNOS_SPIDevice *dev);
SRNOS_SPIStatus SRNOS_SPIGetSnapshot(SRNOS_SPIBusId bus_id,
                                      SRNOS_SPISnapshot *out);
/* Nonblocking: affects only the active request observed at entry. BUSY means
 * recovery is pending; queued and still-copying submissions remain owned.
 * ERROR means quarantined; recovering it requires a system restart. */
SRNOS_SPIStatus SRNOS_SPIRecoverBus(SRNOS_SPIBusId bus_id,
                                     uint32_t reason);




#ifdef __cplusplus
}
#endif

#endif //SRNOS_SPI_H
