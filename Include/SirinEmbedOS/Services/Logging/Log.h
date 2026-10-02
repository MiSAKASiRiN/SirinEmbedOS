#pragma once
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    SRNOS_LOG_DEBUG = 0,
    SRNOS_LOG_INFO  = 1,
    SRNOS_LOG_WARN  = 2,
    SRNOS_LOG_ERROR = 3,
} SRNOS_LogLevel;
#define SRNOS_LOG_SRC_MAX   24u
#define SRNOS_LOG_BODY_MAX  256u
#define SRNOS_LOG_LINE_MAX  384u
#define SRNOS_LOG_BACKEND_USB_CDC          (1u << 0)
#define SRNOS_LOG_BACKEND_SWO              (1u << 1)
#define SRNOS_LOG_BACKEND_UART  (1u << 2)

#define SRNOS_LOG_LEVEL_MASK_DEBUG         (1u << SRNOS_LOG_DEBUG)
#define SRNOS_LOG_LEVEL_MASK_INFO          (1u << SRNOS_LOG_INFO)
#define SRNOS_LOG_LEVEL_MASK_WARN          (1u << SRNOS_LOG_WARN)
#define SRNOS_LOG_LEVEL_MASK_ERROR         (1u << SRNOS_LOG_ERROR)
#define SRNOS_LOG_LEVEL_MASK_ALL 15u
typedef enum { SRNOS_LOG_OK=0, SRNOS_LOG_ERROR_RESULT=-1, SRNOS_LOG_TIMEOUT=-2,
 SRNOS_LOG_FULL=-3, SRNOS_LOG_INVALID=-4, SRNOS_LOG_NO_MEMORY=-5,
 SRNOS_LOG_ISR_FORBIDDEN=-6 } SRNOS_LogResult;
typedef struct {
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
} SRNOS_LogStats;
/* Task context; bounded nonblocking enqueue. Full queues discard an old record.
 * A resource failure never falls back to synchronous output during scheduling. */
SRNOS_LogResult SRNOS_LogPrintf(SRNOS_LogLevel level, const char *source, const char *fmt, ...);
SRNOS_LogResult SRNOS_LogVPrintf(SRNOS_LogLevel level, const char *source, const char *fmt, va_list args);
void SRNOS_LogGetStats(SRNOS_LogStats *out);
void SRNOS_LogSetBackendMask(uint32_t mask);
uint32_t SRNOS_LogGetBackendMask(void);
#ifdef __cplusplus
}
#endif
#define SRNOS_LOGD(...) SRNOS_LogPrintf(SRNOS_LOG_DEBUG, NULL, __VA_ARGS__)
#define SRNOS_LOGD_SRC(src, ...) SRNOS_LogPrintf(SRNOS_LOG_DEBUG, src, __VA_ARGS__)
#define SRNOS_LOGI(...) SRNOS_LogPrintf(SRNOS_LOG_INFO, NULL, __VA_ARGS__)
#define SRNOS_LOGI_SRC(src, ...) SRNOS_LogPrintf(SRNOS_LOG_INFO, src, __VA_ARGS__)
#define SRNOS_LOGW(...) SRNOS_LogPrintf(SRNOS_LOG_WARN, NULL, __VA_ARGS__)
#define SRNOS_LOGW_SRC(src, ...) SRNOS_LogPrintf(SRNOS_LOG_WARN, src, __VA_ARGS__)
#define SRNOS_LOGE(...) SRNOS_LogPrintf(SRNOS_LOG_ERROR, NULL, __VA_ARGS__)
#define SRNOS_LOGE_SRC(src, ...) SRNOS_LogPrintf(SRNOS_LOG_ERROR, src, __VA_ARGS__)
