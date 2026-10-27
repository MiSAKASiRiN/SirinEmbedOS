//
// Watch.h
// Debugger-readable watch variables for STM32H7 Frameworks modules.
//

#ifndef SRNOS_DEBUG_WATCH_H
#define SRNOS_DEBUG_WATCH_H

#pragma once

#include <stdint.h>
#include <stddef.h>

#include "SirinEmbedOS/Core/Memory/MemorySections.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SRNOS_DBG_MAGIC       (0x4F4D5844u) /* "OMXD" */
#define SRNOS_DBG_VERSION     (1u)

typedef enum
{
    SRNOS_DBG_MODULE_SPI       = 1u,
    SRNOS_DBG_MODULE_I2C       = 2u,
    SRNOS_DBG_MODULE_MCP2518FD = 3u,
    SRNOS_DBG_MODULE_BMI088    = 4u,
    SRNOS_DBG_MODULE_DWT       = 5u,
    SRNOS_DBG_MODULE_MPU_DCACHE = 6u,
    SRNOS_DBG_MODULE_EXFDCAN   = 7u,
    SRNOS_DBG_MODULE_IMU       = 8u,
    SRNOS_DBG_MODULE_INS       = 9u,
    SRNOS_DBG_MODULE_LOG   = 10u,
    SRNOS_DBG_MODULE_DR16      = 11u,
    SRNOS_DBG_MODULE_VT03      = 12u,
    SRNOS_DBG_MODULE_PARAMS    = 14u,
    SRNOS_DBG_MODULE_RESERVED_15 = 15u,
    SRNOS_DBG_MODULE_SIRIN_LINK  = 16u,
    SRNOS_DBG_MODULE_USB_CDC   = 17u,
} SRNOS_DbgModuleId;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint16_t module_id;
    uint16_t instance;
    volatile uint32_t seq;
    uint32_t flags;
    uint32_t last_tick;
    uint32_t last_status;
} SRNOS_DbgHeader;

static inline void SRNOS_DbgZero(volatile void *obj, size_t size)
{
    volatile uint8_t *p = (volatile uint8_t *)obj;
    while (size-- > 0u)
    {
        *p++ = 0u;
    }
}

static inline void SRNOS_DbgInit(volatile SRNOS_DbgHeader *h,
                                uint16_t module_id,
                                uint16_t instance,
                                uint16_t size)
{
    if (h == NULL)
    {
        return;
    }

    h->magic = SRNOS_DBG_MAGIC;
    h->version = SRNOS_DBG_VERSION;
    h->size = size;
    h->module_id = module_id;
    h->instance = instance;
    h->seq = 0u;
    h->flags = 0u;
    h->last_tick = 0u;
    h->last_status = 0u;
}

static inline void SRNOS_DbgBegin(volatile SRNOS_DbgHeader *h)
{
    if (h != NULL)
    {
        h->seq++;
    }
}

static inline void SRNOS_DbgEnd(volatile SRNOS_DbgHeader *h,
                               uint32_t tick,
                               uint32_t status)
{
    if (h != NULL)
    {
        h->last_tick = tick;
        h->last_status = status;
        h->seq++;
    }
}

typedef struct
{
    SRNOS_DbgHeader h;
    uint32_t xfer_start;
    uint32_t xfer_done;
    uint32_t xfer_busy;
    uint32_t xfer_error;
    uint32_t xfer_timeout;
    uint32_t last_len;
    uint32_t state;
    uint32_t hal_state;
    uint32_t hal_error;
    uint32_t cb_status;
    uintptr_t current_dev;
    uintptr_t last_dev;
    uintptr_t last_cs_port;
    uint32_t last_cs_pin;
    uint32_t lock_held;
    uintptr_t lock_owner;
    uint32_t lock_last_fail;
    uint32_t lock_os_status;
    uint32_t lock_release_status;
    uint32_t lock_release_error;
    uint32_t lock_ok;
    uint32_t lock_busy;
    uint32_t lock_error;
    uint32_t lock_release;
    uint32_t cb_queued;
    uint32_t cb_queue_overflow;
    uint32_t cb_thread_wake;
    uint32_t recover_count;
    uint32_t recover_error;
    uint32_t last_recover_reason;
    uint32_t launch_guard;
    uint32_t pending_status;
    uint32_t early_complete;
    uint32_t deferred_publish;
    uint32_t spurious_complete;
    uint32_t pending_overwrite;
    uint32_t start_conflict;
    uint32_t last_hal_ret;
} SRNOS_DbgSPI;

typedef struct
{
    SRNOS_DbgHeader h;
    uint32_t xfer_start;
    uint32_t xfer_done;
    uint32_t xfer_busy;
    uint32_t xfer_error;
    uint32_t xfer_timeout;
    uint32_t last_len;
    uint32_t state;
    uint32_t hal_state;
    uint32_t hal_error;
    uintptr_t current_dev;
    uintptr_t last_dev;
    uint16_t mem_addr;
    uint16_t mem_addr_size;
    uint8_t xfer_type;
    uint8_t direct_xfer;
    uint8_t rx_copy_back;
    uint8_t reserved0;
    uint32_t cb_status;
    uint32_t lock_held;
    uintptr_t lock_owner;
    uint32_t lock_last_fail;
    uint32_t lock_os_status;
    uint32_t lock_release_status;
    uint32_t lock_release_error;
    uint32_t lock_ok;
    uint32_t lock_busy;
    uint32_t lock_error;
    uint32_t lock_release;
    uint32_t cb_queued;
    uint32_t cb_queue_overflow;
    uint32_t cb_thread_wake;
    uint32_t recover_count;
    uint32_t recover_error;
    uint32_t last_recover_reason;
} SRNOS_DbgI2C;

typedef struct
{
    SRNOS_DbgHeader h;
    uint32_t xfer_calls;
    uint32_t xfer_ok;
    uint32_t xfer_busy;
    uint32_t xfer_timeout;
    uint32_t xfer_error;
    uint32_t last_ret;
    uint32_t last_size;
    uint32_t last_index;
} SRNOS_DbgMcp2518fd;

typedef struct
{
    SRNOS_DbgHeader h;
    uint32_t phase;
    uint32_t sample_start_tick;
    uint32_t last_sample_tick;
    uint32_t sample_seq;
    uint32_t stale_count;
    uint32_t spi_error_count;
    uint32_t config_error_count;
} SRNOS_DbgBMI088;

typedef struct
{
    SRNOS_DbgHeader h;
    uint32_t ready;
    uint32_t cpu_hz;
    uint32_t init_count;
    uint32_t reset_count;
    uint32_t fallback_count;
    uint32_t last_counter;
    uint32_t last_delta_us;
} SRNOS_DbgTime;

typedef struct
{
    SRNOS_DbgHeader h;
    uint32_t mem_sections_enabled;
    uint32_t init_count;
    uint32_t dma_nc_size;
    uint32_t dma_nc_buf_size;
    uint32_t bdma_buf_size;
    uint32_t eth_dma_size;
    uint32_t dtcm_bss_size;
    uint32_t dtcm_heap_size;
    uint32_t debug_watch_size;
    uint32_t itcm_text_size;
    uint32_t itcm_bss_size;
} SRNOS_DbgMpuDcache;

typedef struct
{
    SRNOS_DbgHeader h;
    uint32_t rx_irq;
    uint32_t tx_irq;
    uint32_t int_irq;
    uint32_t rx_frames;
    uint32_t tx_frames;
    uint32_t rx_overflow;
    uint32_t tx_full;
    uint32_t rx_errors;
    uint32_t tx_errors;
    uint32_t rx_drain_limit;
    uint32_t tx_drain_limit;
    uint32_t init_errors;
    uint32_t module_events;
    uint32_t isr_int0;
    uint32_t isr_int1;
    uint32_t rx_irq_no_rxif;
    uint32_t tx_irq_no_txif;
    uint32_t int_master_wake;
    uint32_t last_int_flags;
    uint32_t last_rx_status;
    uint32_t last_tx_status;
    uint32_t rx_hw_overflow;
    uint32_t rx_status_errors;
    uint32_t rx_read_errors;
} SRNOS_DbgExternalCAN;

typedef struct
{
    SRNOS_DbgHeader h;
    uint32_t app_state;
    uint32_t sample_ok;
    uint32_t sample_busy;
    uint32_t sample_stale;
    uint32_t sample_error;
    uint32_t recovery_count;
    uint32_t recovery_active;
    uint32_t internal_online;
    uint32_t external_online;
    uint32_t heater_pid_status;
    uint32_t heater_pid_saturated;
    uint32_t heater_pid_slew_limited;
    uint32_t heater_fault_latched;
    float temperature_c;
    float heater_pid_error;
    float heater_pid_p;
    float heater_pid_i;
    float heater_pid_d;
    float heater_pid_raw_output;
    float heater_pid_output;
    float heater_pid_dt_s;
    float internal_euler_rad[3];
    float external_euler_rad[3];
} SRNOS_DbgIMU;

typedef struct
{
    SRNOS_DbgHeader h;
    uint32_t app_state;
    uint32_t update_count;
    uint32_t attitude_seeded;
    uint32_t seed_sample_count;
    float dt_s;
    float acc_norm;
    float gyro_norm;
    float SRNOS_MahonyKpEff;
    float raw_euler_rad[3];
    float euler_rad[3];
    float gyro_only_rad[3];
} SRNOS_DbgIns;

typedef struct
{
    SRNOS_DbgHeader h;
    uint32_t queued;
    uint32_t printed;
    uint32_t dropped_queue;
    uint32_t dropped_backend;
    uint32_t truncated_body;
    uint32_t direct_printed;
    uint32_t uart_dma_error;
    uint32_t uart_busy;
    uint32_t uart_drop;
    uint32_t swo_drop;
    uint32_t backend_mask;
    uint32_t last_line_len;
    uint32_t last_backend_status;
    uint32_t rx_enabled;
    uint32_t rx_bytes;
    uint32_t rx_events;
    uint32_t rx_overflow_bytes;
    uint32_t rx_errors;
    uint32_t last_rx_size;
    uint32_t tx_high_prio_bytes;
    uint32_t tx_high_prio_writes;
    uint32_t tx_high_prio_full;
    uint32_t tx_high_prio_last_len;
    uint32_t tx_high_prio_pending;
    uint32_t tx_normal_pending;
    uint32_t usb_configured;
    uint32_t usb_tx_bytes;
    uint32_t usb_rx_bytes;
    uint32_t usb_tx_drop;
    uint32_t usb_rx_drop;
    uint32_t usb_tx_busy;
    uint32_t usb_tx_errors;
    uint32_t usb_rx_overflow;
    uint32_t usb_rx_pending;
    uint32_t uart_bytes;
    uint32_t uart_writes;
    uint32_t uart_dropped_bytes;
} SRNOS_DbgLog;

typedef struct
{
    SRNOS_DbgHeader h;
    uintptr_t huart;
    uint32_t started;
    uint32_t rx_dma_buf_size;
    uint32_t rx_fifo_size;
    uint32_t fifo_head;
    uint32_t fifo_tail;
    uint32_t fifo_used;
    uint32_t rx_evt_idle_count;
    uint32_t rx_evt_tc_count;
    uint32_t rx_hw_error_count;
    uint32_t rx_recover_count;
    uint32_t rx_fifo_overflow_bytes;
    uint32_t rx_framer_drop_count;
    uint32_t rx_frame_ok_count;
    uint32_t rx_start_ret;
    uint32_t last_rx_size;
    uint32_t last_poll_bytes;
    uint32_t last_poll_frames;
} SRNOS_DbgDR16;

typedef struct
{
    SRNOS_DbgHeader h;
    uintptr_t huart;
    uint32_t started;
    uint32_t rx_dma_buf_size;
    uint32_t rx_fifo_size;
    uint32_t fifo_head;
    uint32_t fifo_tail;
    uint32_t fifo_used;
    uint32_t rx_evt_idle_count;
    uint32_t rx_evt_tc_count;
    uint32_t rx_hw_error_count;
    uint32_t rx_recover_count;
    uint32_t rx_fifo_overflow_bytes;
    uint32_t rx_framer_drop_count;
    uint32_t rx_frame_ok_count;
    uint32_t rx_start_ret;
    uint32_t last_rx_size;
    uint32_t last_poll_bytes;
    uint32_t last_poll_frames;
} SRNOS_DbgVT03;



typedef struct
{
    SRNOS_DbgHeader h;
    uint32_t registered;
    uint32_t max_count;
    uint32_t register_errors;
    uint32_t duplicate_errors;
    uint32_t payload_size;
    uint32_t eeprom_start_addr;
    uint32_t eeprom_capacity;
    uint32_t layout_hash;
    uint32_t init_count;
    uint32_t load_count;
    uint32_t load_ok;
    uint32_t save_count;
    uint32_t save_ok;
    uint32_t crc_error;
    uint32_t layout_mismatch;
    uint32_t bad_data;
    uint32_t range_error;
    uint32_t no_space;
    uint32_t eeprom_error;
    uint32_t parse_error;
    uint32_t not_found;
    uint32_t set_ok;
    uint32_t last_error;
} SRNOS_DbgParams;

typedef struct
{
    uint32_t rx_frames;
    uint32_t rx_bytes;
    uint32_t tx_frames;
    uint32_t tx_bytes;
    uint32_t submitted;
    uint32_t sent;
    uint32_t rejected;
    uint32_t drop_new;
    uint32_t drop_old;
    uint32_t coalesced;
    uint32_t queue_depth;
    uint32_t queue_high_water;
    uint32_t last_wait_ms;
    uint32_t max_wait_ms;
} SRNOS_DbgSirinLinkQos;

typedef struct
{
    SRNOS_DbgHeader h;
    uint32_t started;
    uint32_t usb_configured;
    uint32_t rx_frames;
    uint32_t rx_bytes;
    uint32_t tx_frames;
    uint32_t tx_bytes;
    uint32_t cobs_errors;
    uint32_t crc_errors;
    uint32_t length_errors;
    uint32_t version_errors;
    uint32_t flags_errors;
    uint32_t kind_errors;
    uint32_t unknown_service;
    uint32_t unknown_message;
    uint32_t rx_overflow;
    uint32_t sequence_gap;
    uint32_t sequence_duplicate;
    uint32_t sequence_out_of_order;
    uint32_t request_replay;
    uint32_t request_conflict;
    uint32_t transport_busy;
    uint32_t transport_errors;
    uint32_t transport_disconnects;
    uint32_t processing_cycles_total;
    uint32_t processing_cycles_max;
    uint32_t transport_connected;
    uint32_t comm_stack_high_water;
    uint32_t worker_stack_high_water;
    uint32_t deferred_depth;
    uint32_t deferred_high_water;
    uint32_t source_registered;
    uint32_t source_active;
    uint32_t source_total_rate_hz;
    uint32_t source_batches;
    uint32_t source_records;
    uint32_t source_read_failures;
    uint32_t source_publish_failures;
    uint32_t source_missed_periods;
    uint32_t source_cycles_total;
    uint32_t source_cycles_max;
    SRNOS_DbgSirinLinkQos qos[4];
} SRNOS_DbgSirinLink;

typedef struct
{
    SRNOS_DbgHeader h;
    uint32_t configured;
    uint32_t dtr;
    uint32_t tx_active;
    uint32_t rx_events;
    uint32_t rx_bytes;
    uint32_t rx_drop;
    uint32_t tx_events;
    uint32_t tx_bytes;
    uint32_t tx_complete;
    uint32_t tx_busy;
    uint32_t tx_errors;
    uint32_t tx_drop;
    uint32_t disconnects;
    uint32_t last_rx_len;
    uint32_t last_tx_len;
    uint32_t queue_depth;
    uint32_t queue_high_water;
    uint32_t replay_active;
    uint32_t replay_restarts;
} SRNOS_DbgUSBCdc;

extern volatile SRNOS_DbgSPI SRNOS_DebugDbgSPI[];
extern volatile SRNOS_DbgI2C SRNOS_DebugDbgI2C[];
extern volatile SRNOS_DbgMcp2518fd SRNOS_DebugDbgMcp2518fd[];
extern volatile SRNOS_DbgBMI088 SRNOS_DebugDbgBMI088;
extern volatile SRNOS_DbgTime SRNOS_DebugDbgTime;
extern volatile SRNOS_DbgMpuDcache SRNOS_DebugDbgMpuDcache;
extern volatile SRNOS_DbgExternalCAN SRNOS_DebugDbgExternalCAN[];
extern volatile SRNOS_DbgIMU SRNOS_DebugDbgIMU;
extern volatile SRNOS_DbgIns SRNOS_DebugDbgIns;
extern volatile SRNOS_DbgLog SRNOS_DebugLog;
extern volatile SRNOS_DbgDR16 SRNOS_DebugDbgDR16[];
extern volatile SRNOS_DbgVT03 SRNOS_DebugDbgVT03[];
extern volatile SRNOS_DbgParams SRNOS_DebugDbgParams;
extern volatile SRNOS_DbgSirinLink SRNOS_DebugSirinLink;
extern volatile SRNOS_DbgUSBCdc SRNOS_DebugDbgUSBCdc[];

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SRNOS_DEBUG_WATCH_H */
