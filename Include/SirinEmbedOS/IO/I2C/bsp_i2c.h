#ifndef SRNOS_IO_I2C_H
#define SRNOS_IO_I2C_H
#include "stm32h7xx_hal.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#if defined(SRNOS_MEMORY_SECTIONS) && SRNOS_MEMORY_SECTIONS
#include "SirinEmbedOS/Core/Memory/mem_sections.h"
#define BSP_I2C_DMA_SEC OMX_BDMA_BUF_BSS
#else
#define BSP_I2C_DMA_SEC
#endif
#ifndef BSP_I2C_DMA_STAGING_SIZE
#define BSP_I2C_DMA_STAGING_SIZE 256U
#endif
/* Limit a non-preemptible transaction on the fixed ~100 kHz I2C4 bus.
 * Device drivers must split longer operations respecting device semantics. */
#define BSP_I2C_MAX_TRANSFER_BYTES 64U
#define BSP_I2C_PERIODIC_WAIT_MS 30U
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BSP_I2C_OK = 0, BSP_I2C_ERROR, BSP_I2C_BUSY, BSP_I2C_INVALID_PARAM,
    BSP_I2C_TIMEOUT, BSP_I2C_CANCELLED
} bsp_i2c_status_t;
typedef enum { BSP_I2C_BUS_4 = 0, BSP_I2C_BUS_MAX } bsp_i2c_bus_id_t;
struct bsp_i2c_bus;
/* Descriptors have static/operation lifetime. Initialize once before use.
 * The owner must serialize submit/wait on each handle across tasks.
 * One outstanding request per physical address, even across different handles.
 * pending covers queued + active work, cleared after all RX accesses finish.
 * With a callback, user context/handle must live until the callback returns. */
typedef struct {
    struct bsp_i2c_bus *bus;
    uint16_t addr_7bit;
    uint32_t max_speed_hz;
    volatile uint8_t pending;
    volatile bsp_i2c_status_t completed_status;
    uint32_t flags;
} bsp_i2c_device_t;
typedef void (*bsp_i2c_callback_t)(bsp_i2c_device_t *, void *, bsp_i2c_status_t);

typedef struct {
    uint32_t accepted, completed, errors, timeouts, cancelled, queue_full;
    uint32_t max_queue_us, max_transfer_us;
} bsp_i2c_class_statistics_t;
typedef struct {
    uint32_t state, hal_state, hal_error, lock_held;
    uintptr_t lock_owner, current_dev;
    uint32_t current_len, last_status, last_tick, recover_count;
    uint32_t cb_queued, cb_queue_overflow; /* No separate callback queue now. */
    uint32_t queued, high_water, quarantined;
    bsp_i2c_class_statistics_t periodic, background;
} bsp_i2c_snapshot_t;
#define BSP_I2C_RECOVER_REASON_TIMEOUT 1U
#define BSP_I2C_RECOVER_REASON_USER 255U

/* Startup only: init after HAL/BDMA; init_rtos after osKernelInitialize and
 * before osKernelStart. Reuses the former callback thread: High6, 3 KiB.
 * Creates four completion semaphores; no runtime task/allocation/rebind. */
void bsp_i2c_init(void);
void bsp_i2c_init_rtos(void);
bsp_i2c_status_t bsp_i2c_device_init(bsp_i2c_device_t *, bsp_i2c_bus_id_t, uint16_t addr_7bit);

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
bsp_i2c_status_t bsp_i2c_master_transmit(bsp_i2c_device_t *, const uint8_t *, size_t, bsp_i2c_callback_t, void *);
bsp_i2c_status_t bsp_i2c_master_receive(bsp_i2c_device_t *, uint8_t *, size_t, bsp_i2c_callback_t, void *);
bsp_i2c_status_t bsp_i2c_mem_read(bsp_i2c_device_t *, uint16_t, uint16_t, uint8_t *, size_t, bsp_i2c_callback_t, void *);
bsp_i2c_status_t bsp_i2c_mem_write(bsp_i2c_device_t *, uint16_t, uint16_t, const uint8_t *, size_t, bsp_i2c_callback_t, void *);
/* Existing direct entry points now use the SAME queue, copy and 64 B bound.
 * They no longer bypass ownership or expose user RAM to DMA. */
bsp_i2c_status_t bsp_i2c_master_transmit_direct(bsp_i2c_device_t *, const uint8_t *, size_t, bsp_i2c_callback_t, void *);
bsp_i2c_status_t bsp_i2c_master_receive_direct(bsp_i2c_device_t *, uint8_t *, size_t, bsp_i2c_callback_t, void *);
bsp_i2c_status_t bsp_i2c_mem_read_direct(bsp_i2c_device_t *, uint16_t, uint16_t, uint8_t *, size_t, bsp_i2c_callback_t, void *);
bsp_i2c_status_t bsp_i2c_mem_write_direct(bsp_i2c_device_t *, uint16_t, uint16_t, const uint8_t *, size_t, bsp_i2c_callback_t, void *);

/* Wait using a completion semaphore, not tick polling. One waiter per device.
 * timeout_ms starts at this wait and includes remaining queueing. Expiry cancels only this request and waits for
 * dispatcher acknowledgement so returning always releases the user's buffer.
 * Cleanup may add a bounded abort (5 ms + scheduling/callback overhead).
 * Already completed requests return the retained device result. */
bsp_i2c_status_t bsp_i2c_wait_complete(bsp_i2c_device_t *, uint32_t timeout_ms);
/* Blocking ACK probe is queued too. Worker performs ONE <=1 ms HAL probe at
 * a time; trials are separate transactions, bounded by caller timeout_ms.
 * No EEPROM write-cycle loop runs in the dispatcher. */
bsp_i2c_status_t bsp_i2c_device_ready(bsp_i2c_device_t *, uint32_t trials, uint32_t timeout_ms);
/* Task-only cancel, waits until matching request/callback is no longer using
 * buffers. Queued requests are removed without HAL abort. Active cancellation
 * confirms HAL/BDMA stop; failure quarantines bus and rejects future transfers.
 * A completed request never aborts a subsequent request of another device. */
bsp_i2c_status_t bsp_i2c_cancel_device(bsp_i2c_device_t *, uint32_t reason);
/* Cancels only the active request observed at entry, preserving queued work
 * when abort succeeds. A quarantined bus requires system restart. */
bsp_i2c_status_t bsp_i2c_recover_bus(bsp_i2c_bus_id_t, uint32_t reason);
bsp_i2c_status_t bsp_i2c_is_bus_busy(const bsp_i2c_device_t *);
bsp_i2c_status_t bsp_i2c_get_snapshot(bsp_i2c_bus_id_t, bsp_i2c_snapshot_t *);
/* Raw bus acquire/release removed: all hardware access belongs to dispatcher. */
#ifdef __cplusplus
}
#endif
#endif
