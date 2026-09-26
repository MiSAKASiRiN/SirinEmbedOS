//
// Created by sirin on 2025/11/29.
//

#ifndef H723VG_V2_FREERTOS_BSP_SPI_H
#define H723VG_V2_FREERTOS_BSP_SPI_H
#include "stm32h7xx_hal.h"
#include "SirinEmbedOS/Diagnostics/Watch/omx_debug_watch.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Cache / DMA buffer compatibility
 * ============================================================
 *
 * Default transfer APIs copy caller buffers through internal non-cacheable
 * staging buffers, so ordinary callers do not need to place their buffers in
 * a special section. BSP_SPI_DMA_SEC is kept only for legacy/direct users.
 */
#if defined(SRNOS_MEMORY_SECTIONS) && SRNOS_MEMORY_SECTIONS
#include "SirinEmbedOS/Core/Memory/mem_sections.h"
#define BSP_SPI_DMA_SEC OMX_DMA_NC_BUF_BSS
#else
#define BSP_SPI_DMA_SEC
#endif

#ifndef BSP_SPI_DMA_STAGING_SIZE
#define BSP_SPI_DMA_STAGING_SIZE (512u)
#endif

/* ============================================================
 * ENUMS
 * ============================================================ */
typedef enum
{
    BSP_SPI_OK = 0,
    BSP_SPI_BUSY,
    BSP_SPI_ERROR,
    BSP_SPI_INVALID_PARAM
} bsp_spi_status_t;

typedef enum
{
    BSP_SPI_BUS_4 = 0,
    BSP_SPI_BUS_MAX
} bsp_spi_bus_id_t;

struct bsp_spi_bus;

/* ============================================================
 * SPI Device Structure
 * ============================================================ */
typedef struct
{
    struct bsp_spi_bus *bus;
    GPIO_TypeDef       *cs_port;
    uint16_t            cs_pin;

    uint32_t            max_speed_hz;
    uint8_t             mode;
    uint8_t             bits_per_word;
} bsp_spi_device_t;

/* Callback type */
typedef void (*bsp_spi_callback_t)(bsp_spi_device_t *dev, void *user_ctx, bsp_spi_status_t status);

/* lock_last_fail codes */
#define BSP_SPI_LOCK_FAIL_NONE              0u
#define BSP_SPI_LOCK_FAIL_IN_ISR            1u
#define BSP_SPI_LOCK_FAIL_KERNEL_NOT_RUNNING 2u
#define BSP_SPI_LOCK_FAIL_ALREADY_HELD      3u
#define BSP_SPI_LOCK_FAIL_SEM_NULL          4u
#define BSP_SPI_LOCK_FAIL_TIMEOUT           5u
#define BSP_SPI_LOCK_FAIL_OS_RESOURCE       6u
#define BSP_SPI_LOCK_FAIL_OS_ERROR          7u

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
} bsp_spi_snapshot_t;

#define BSP_SPI_RECOVER_REASON_TIMEOUT       1u
#define BSP_SPI_RECOVER_REASON_BMI088_STALE  2u
#define BSP_SPI_RECOVER_REASON_USER          255u

/* ============================================================
 * PUBLIC API
 * ============================================================ */

void bsp_spi_init_pre(void);
void bsp_spi_init_rtos(void);

bsp_spi_status_t bsp_spi_device_init(bsp_spi_device_t *dev,
                                     bsp_spi_bus_id_t bus_id,
                                     GPIO_TypeDef *cs_port,
                                     uint16_t cs_pin);

/**
 * @brief Start an async DMA transfer through internal staging buffers.
 *
 * This is the default safe API. tx_buf/rx_buf may live in normal cacheable RAM,
 * but len must not exceed BSP_SPI_DMA_STAGING_SIZE. The callback runs from HAL
 * bsp_spi_cb task context after the transfer owner has released the bus.
 */
bsp_spi_status_t bsp_spi_transfer_dma(bsp_spi_device_t *dev,
                                      const uint8_t *tx_buf,
                                      uint8_t *rx_buf,
                                      size_t len,
                                      bsp_spi_callback_t cb,
                                      void *user_ctx);

/**
 * @brief Start an async DMA transfer using caller buffers directly.
 *
 * Use this only when caller buffers are DMA-safe for the active cache policy
 * (for example BSP_SPI_DMA_SEC/static non-cacheable buffers, or explicit cache
 * maintenance). No staging copy is performed and len is not limited by
 * BSP_SPI_DMA_STAGING_SIZE.
 */
bsp_spi_status_t bsp_spi_transfer_dma_direct(bsp_spi_device_t *dev,
                                             const uint8_t *tx_buf,
                                             uint8_t *rx_buf,
                                             size_t len,
                                             bsp_spi_callback_t cb,
                                             void *user_ctx);

/**
 * Blocking wrapper using the default safe staging path.
 */
bsp_spi_status_t bsp_spi_transfer_dma_blocking(bsp_spi_device_t *dev,
                                               const uint8_t *tx_buf,
                                               uint8_t *rx_buf,
                                               size_t len,
                                               uint32_t timeout_ms);

/**
 * Unified transfer (blocking or async), default safe staging path.
 */
bsp_spi_status_t bsp_spi_transfer_ex(bsp_spi_device_t *dev,
                                     const uint8_t *tx_buf,
                                     uint8_t *rx_buf,
                                     size_t len,
                                     bsp_spi_callback_t cb,
                                     void *user_ctx,
                                     uint8_t blocking,
                                     uint32_t timeout_ms);

/**
 * Unified transfer (blocking or async), direct caller-buffer path.
 */
bsp_spi_status_t bsp_spi_transfer_direct_ex(bsp_spi_device_t *dev,
                                            const uint8_t *tx_buf,
                                            uint8_t *rx_buf,
                                            size_t len,
                                            bsp_spi_callback_t cb,
                                            void *user_ctx,
                                            uint8_t blocking,
                                            uint32_t timeout_ms);

/**
 * Convenience wrappers
 */
static inline bsp_spi_status_t bsp_spi_transfer_blocking(bsp_spi_device_t *dev,
                                                         const uint8_t *tx,
                                                         uint8_t *rx,
                                                         size_t len,
                                                         uint32_t timeout)
{
    return bsp_spi_transfer_ex(dev, tx, rx, len, NULL, NULL, 1, timeout);
}

static inline bsp_spi_status_t bsp_spi_transfer_async(bsp_spi_device_t *dev,
                                                      const uint8_t *tx,
                                                      uint8_t *rx,
                                                      size_t len,
                                                      bsp_spi_callback_t cb,
                                                      void *ctx)
{
    return bsp_spi_transfer_ex(dev, tx, rx, len, cb, ctx, 0, 0);
}

/**
 * Start an async transfer after waiting at most timeout_ms for the bus.
 * Existing bsp_spi_transfer_async() keeps its zero-wait behavior.
 */
static inline bsp_spi_status_t bsp_spi_transfer_async_wait(bsp_spi_device_t *dev,
                                                           const uint8_t *tx,
                                                           uint8_t *rx,
                                                           size_t len,
                                                           bsp_spi_callback_t cb,
                                                           void *ctx,
                                                           uint32_t timeout_ms)
{
    return bsp_spi_transfer_ex(dev, tx, rx, len, cb, ctx, 0, timeout_ms);
}

static inline bsp_spi_status_t bsp_spi_transfer_direct_blocking(bsp_spi_device_t *dev,
                                                                const uint8_t *tx,
                                                                uint8_t *rx,
                                                                size_t len,
                                                                uint32_t timeout)
{
    return bsp_spi_transfer_direct_ex(dev, tx, rx, len, NULL, NULL, 1, timeout);
}

static inline bsp_spi_status_t bsp_spi_transfer_direct_async(bsp_spi_device_t *dev,
                                                             const uint8_t *tx,
                                                             uint8_t *rx,
                                                             size_t len,
                                                             bsp_spi_callback_t cb,
                                                             void *ctx)
{
    return bsp_spi_transfer_direct_ex(dev, tx, rx, len, cb, ctx, 0, 0);
}

bsp_spi_status_t bsp_spi_is_bus_busy(const bsp_spi_device_t *dev);
bsp_spi_status_t bsp_spi_get_snapshot(bsp_spi_bus_id_t bus_id,
                                      bsp_spi_snapshot_t *out);
bsp_spi_status_t bsp_spi_recover_bus(bsp_spi_bus_id_t bus_id,
                                     uint32_t reason);

/**
 * Bus-level acquire/release (DMA lifetime lock).
 * - timeout_ms == 0: try-acquire (no wait)
 * - async completion releases in ISR, unless completion arrived before the HAL
 *   launch call returned; blocking completion is released by its owner task
 */
bsp_spi_status_t bsp_spi_bus_acquire(bsp_spi_device_t *dev, uint32_t timeout_ms);
void bsp_spi_bus_release_isr(const bsp_spi_device_t *dev);

#ifdef __cplusplus
}
#endif

#endif //H723VG_V2_FREERTOS_BSP_SPI_H
