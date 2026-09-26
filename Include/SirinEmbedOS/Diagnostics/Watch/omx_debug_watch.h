//
// omx_debug_watch.h
// Debugger-readable watch variables for STM32H7 Frameworks modules.
//

#ifndef OMX_DEBUG_WATCH_H
#define OMX_DEBUG_WATCH_H

#pragma once

#include <stdint.h>
#include <stddef.h>

#include "SirinEmbedOS/Core/Memory/mem_sections.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OMX_DBG_MAGIC       (0x4F4D5844u) /* "OMXD" */
#define OMX_DBG_VERSION     (1u)

typedef enum
{
    OMX_DBG_MODULE_SPI       = 1u,
    OMX_DBG_MODULE_I2C       = 2u,
    OMX_DBG_MODULE_MCP2518FD = 3u,
    OMX_DBG_MODULE_BMI088    = 4u,
    OMX_DBG_MODULE_DWT       = 5u,
    OMX_DBG_MODULE_MPU_DCACHE = 6u,
    OMX_DBG_MODULE_EXFDCAN   = 7u,
    OMX_DBG_MODULE_IMU       = 8u,
    OMX_DBG_MODULE_INS       = 9u,
    OMX_DBG_MODULE_SRN_LOG   = 10u,
    OMX_DBG_MODULE_DR16      = 11u,
    OMX_DBG_MODULE_VT03      = 12u,
    OMX_DBG_MODULE_REMOTE_CONTROL = 13u,
    OMX_DBG_MODULE_PARAMS    = 14u,
    OMX_DBG_MODULE_RESERVED_15 = 15u,
    OMX_DBG_MODULE_SRN_LINK  = 16u,
    OMX_DBG_MODULE_USB_CDC   = 17u,
} omx_dbg_module_id_t;

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
} omx_dbg_header_t;

static inline void omx_dbg_zero(volatile void *obj, size_t size)
{
    volatile uint8_t *p = (volatile uint8_t *)obj;
    while (size-- > 0u)
    {
        *p++ = 0u;
    }
}

static inline void omx_dbg_init(volatile omx_dbg_header_t *h,
                                uint16_t module_id,
                                uint16_t instance,
                                uint16_t size)
{
    if (h == NULL)
    {
        return;
    }

    h->magic = OMX_DBG_MAGIC;
    h->version = OMX_DBG_VERSION;
    h->size = size;
    h->module_id = module_id;
    h->instance = instance;
    h->seq = 0u;
    h->flags = 0u;
    h->last_tick = 0u;
    h->last_status = 0u;
}

static inline void omx_dbg_begin(volatile omx_dbg_header_t *h)
{
    if (h != NULL)
    {
        h->seq++;
    }
}

static inline void omx_dbg_end(volatile omx_dbg_header_t *h,
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
    omx_dbg_header_t h;
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
} omx_dbg_spi_t;

typedef struct
{
    omx_dbg_header_t h;
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
} omx_dbg_i2c_t;

typedef struct
{
    omx_dbg_header_t h;
    uint32_t xfer_calls;
    uint32_t xfer_ok;
    uint32_t xfer_busy;
    uint32_t xfer_timeout;
    uint32_t xfer_error;
    uint32_t last_ret;
    uint32_t last_size;
    uint32_t last_index;
} omx_dbg_mcp2518fd_t;

typedef struct
{
    omx_dbg_header_t h;
    uint32_t phase;
    uint32_t sample_start_tick;
    uint32_t last_sample_tick;
    uint32_t sample_seq;
    uint32_t stale_count;
    uint32_t spi_error_count;
    uint32_t config_error_count;
} omx_dbg_bmi088_t;

typedef struct
{
    omx_dbg_header_t h;
    uint32_t ready;
    uint32_t cpu_hz;
    uint32_t init_count;
    uint32_t reset_count;
    uint32_t fallback_count;
    uint32_t last_counter;
    uint32_t last_delta_us;
} omx_dbg_dwt_t;

typedef struct
{
    omx_dbg_header_t h;
    uint32_t mem_sections_enabled;
    uint32_t init_count;
    uint32_t dma_nc_size;
    uint32_t dma_nc_buf_size;
    uint32_t bdma_buf_size;
    uint32_t eth_dma_size;
    uint32_t dtcm_bss_size;
    uint32_t dtcm_heap_size;
    uint32_t debug_watch_size;
} omx_dbg_mpu_dcache_t;

typedef struct
{
    omx_dbg_header_t h;
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
} omx_dbg_exfdcan_t;

typedef struct
{
    omx_dbg_header_t h;
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
} omx_dbg_imu_t;

typedef struct
{
    omx_dbg_header_t h;
    uint32_t app_state;
    uint32_t update_count;
    uint32_t attitude_seeded;
    uint32_t seed_sample_count;
    float dt_s;
    float acc_norm;
    float gyro_norm;
    float mahony_kp_eff;
    float raw_euler_rad[3];
    float euler_rad[3];
    float gyro_only_rad[3];
} omx_dbg_ins_t;

typedef struct
{
    omx_dbg_header_t h;
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
    uint32_t uart_fallback_bytes;
    uint32_t uart_fallback_writes;
    uint32_t uart_fallback_drop;
} omx_dbg_srn_log_t;

typedef struct
{
    omx_dbg_header_t h;
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
} omx_dbg_dr16_t;

typedef struct
{
    omx_dbg_header_t h;
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
} omx_dbg_vt03_t;

typedef struct
{
    omx_dbg_header_t h;
    uint32_t active_src;
    uint32_t online;
    uint32_t idle;
    uint32_t blocked;
    uint32_t sticks_idle;
    uint32_t dr16_online;
    uint32_t vt03_online;
    uint32_t last_ok_ms;
    uint32_t source_update_count;
    uint32_t tick_count;
    uint32_t snapshot_get_count;
    uint32_t status_get_count;
    uint32_t priority_primary;
    uint32_t priority_secondary;
    int16_t ch[5];
    uint8_t sw[2];
    uint8_t sw_valid_mask;
    uint8_t reserved0;
    uint16_t key_v;
    uint16_t key_v_ext;
} omx_dbg_remote_control_t;

typedef struct
{
    omx_dbg_header_t h;
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
} omx_dbg_params_t;

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
} omx_dbg_srn_link_qos_t;

typedef struct
{
    omx_dbg_header_t h;
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
    omx_dbg_srn_link_qos_t qos[4];
} omx_dbg_srn_link_t;

typedef struct
{
    omx_dbg_header_t h;
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
} omx_dbg_usb_cdc_t;

extern volatile omx_dbg_spi_t g_omx_dbg_spi[];
extern volatile omx_dbg_i2c_t g_omx_dbg_i2c[];
extern volatile omx_dbg_mcp2518fd_t g_omx_dbg_mcp2518fd[];
extern volatile omx_dbg_bmi088_t g_omx_dbg_bmi088;
extern volatile omx_dbg_dwt_t g_omx_dbg_dwt;
extern volatile omx_dbg_mpu_dcache_t g_omx_dbg_mpu_dcache;
extern volatile omx_dbg_exfdcan_t g_omx_dbg_exfdcan[];
extern volatile omx_dbg_imu_t g_omx_dbg_imu;
extern volatile omx_dbg_ins_t g_omx_dbg_ins;
extern volatile omx_dbg_srn_log_t g_omx_dbg_srn_log;
extern volatile omx_dbg_dr16_t g_omx_dbg_dr16[];
extern volatile omx_dbg_vt03_t g_omx_dbg_vt03[];
extern volatile omx_dbg_remote_control_t g_omx_dbg_remote_control;
extern volatile omx_dbg_params_t g_omx_dbg_params;
extern volatile omx_dbg_srn_link_t g_omx_dbg_srn_link;
extern volatile omx_dbg_usb_cdc_t g_omx_dbg_usb_cdc[];

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OMX_DEBUG_WATCH_H */
