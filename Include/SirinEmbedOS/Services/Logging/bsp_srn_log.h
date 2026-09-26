#pragma once

#include "stm32h7xx_hal.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "SirinEmbedOS/Diagnostics/Watch/omx_debug_watch.h"
#include "SirinEmbedOS/Services/Logging/bsp_srn_debug_port.h"
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SRN_LOG_DEBUG = 0,
    SRN_LOG_INFO  = 1,
    SRN_LOG_WARN  = 2,
    SRN_LOG_ERROR = 3,
} srn_log_level_t;

#ifndef SRN_LOG_SRC_MAX
#define SRN_LOG_SRC_MAX   24u
#endif
#ifndef SRN_LOG_BODY_MAX
#define SRN_LOG_BODY_MAX  256u
#endif
#ifndef SRN_LOG_LINE_MAX
#define SRN_LOG_LINE_MAX  384u
#endif

#define SRN_LOG_BACKEND_USB_CDC          (1u << 0)
#define SRN_LOG_BACKEND_SWO              (1u << 1)
#define SRN_LOG_BACKEND_USART1_FALLBACK  (1u << 2)
#define SRN_LOG_BACKEND_UART             SRN_LOG_BACKEND_USART1_FALLBACK

#define SRN_LOG_LEVEL_MASK_DEBUG         (1u << SRN_LOG_DEBUG)
#define SRN_LOG_LEVEL_MASK_INFO          (1u << SRN_LOG_INFO)
#define SRN_LOG_LEVEL_MASK_WARN          (1u << SRN_LOG_WARN)
#define SRN_LOG_LEVEL_MASK_ERROR         (1u << SRN_LOG_ERROR)
#define SRN_LOG_LEVEL_MASK_ALL           (SRN_LOG_LEVEL_MASK_DEBUG | \
                                          SRN_LOG_LEVEL_MASK_INFO |  \
                                          SRN_LOG_LEVEL_MASK_WARN |  \
                                          SRN_LOG_LEVEL_MASK_ERROR)

#ifndef SRN_LOG_ENABLE_USB_CDC
#define SRN_LOG_ENABLE_USB_CDC           1
#endif
#ifndef SRN_LOG_ENABLE_SWO
#define SRN_LOG_ENABLE_SWO               0
#endif
#ifndef SRN_LOG_ENABLE_USART1_FALLBACK
#define SRN_LOG_ENABLE_USART1_FALLBACK   1
#endif
#ifndef SRN_LOG_USB_LEVEL_MASK
#define SRN_LOG_USB_LEVEL_MASK           SRN_LOG_LEVEL_MASK_ALL
#endif
#ifndef SRN_LOG_SWO_LEVEL_MASK
#define SRN_LOG_SWO_LEVEL_MASK           SRN_LOG_LEVEL_MASK_ALL
#endif
#ifndef SRN_LOG_USART1_LEVEL_MASK
#define SRN_LOG_USART1_LEVEL_MASK        SRN_LOG_LEVEL_MASK_ALL
#endif

typedef enum {
    SRN_LOG_OUTPUT_OK = 0,
    SRN_LOG_OUTPUT_INVALID,
    SRN_LOG_OUTPUT_NO_BACKEND,
    SRN_LOG_OUTPUT_BACKEND_FULL,
} srn_log_output_status_t;

typedef bool (*srn_log_usb_submit_fn)(void *user,
                                      const uint8_t *data,
                                      size_t length);
typedef void (*srn_log_usb_service_fn)(void *user);

typedef struct {
    void *user;
    srn_log_usb_submit_fn submit;
    srn_log_usb_service_fn service;
} srn_log_usb_transport_t;

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
    uint32_t uart_fallback_bytes;
    uint32_t uart_fallback_writes;
    uint32_t uart_fallback_drop;
} srn_log_stats_t;

/* 日志消息内容（真正的数据块，存放在 MemoryPool 的一个 block 里） */
typedef struct {
    uint64_t        ts_ms;               /* 64-bit 单调毫秒（HAL_GetTick 扩展） */
    srn_log_level_t level;
    char            source[SRN_LOG_SRC_MAX];
    char            body[SRN_LOG_BODY_MAX];
    uint8_t         _pad[3];             /* 对齐 */
} srn_log_msg_t;

struct SRNOS_SerialChannel;
bool SRNOS_LogBindUart(struct SRNOS_SerialChannel *channel);
void SRNOS_LogFreezeTransports(void);

/* 初始化（保留扩展；当前仅毫秒时间基，无 DWT） */
void SRN_Log_Init(void);

/* 绑定上层日志 task prototype 创建的队列和内存池。 */
void SRN_Log_BindQueue(osMessageQueueId_t q);
void SRN_Log_BindPool (osMemoryPoolId_t   p);

void SRN_Log_SetBackendMask(uint32_t backend_mask);
uint32_t SRN_Log_GetBackendMask(void);
void SRN_Log_GetStats(srn_log_stats_t* out);
void SRN_Log_RecordDroppedQueue(void);
void SRN_Log_RecordTruncatedBody(void);
void SRN_Log_RecordUartDmaError(void);
void SRN_Log_RecordUartBusy(void);
void SRN_Log_RecordUartDrop(void);

/* Project-owned ASCII USB transport. submit() must copy before returning. */
void SRN_Log_RegisterUsbTransport(const srn_log_usb_transport_t *transport);
void SRN_Log_ServiceUsbTransport(void);

size_t SRN_LogFormatLine(char* out,
                         size_t out_sz,
                         const srn_log_msg_t* msg,
                         bool* truncated);
srn_log_output_status_t SRN_LogOutputLine(const char* line, size_t len);
srn_log_output_status_t SRN_LogOutputLineEx(const char* line,
                                            size_t len,
                                            srn_log_level_t level,
                                            bool boot_line);
void SRN_Log_WriteBootFallback(const char* line);

/* printf 风格 API（在调用点捕获时间戳、将“指针”放入队列；非阻塞，满了丢最旧） */
osStatus_t SRN_LogPrintf(srn_log_level_t level,
                         const char* source,
                         uint32_t timeout_ticks,   /* 保留但忽略，统一非阻塞 */
                         const char* fmt, ...);

osStatus_t SRN_LogVPrintf(srn_log_level_t level,
                          const char* source,
                          uint32_t timeout_ticks,   /* 保留但忽略 */
                          const char* fmt,
                          va_list ap);

/* 便捷宏（可自行调整默认超时；这里忽略） */
#define LOGD(fmt, ...)  SRN_LogPrintf(SRN_LOG_DEBUG, NULL, 0, fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...)  SRN_LogPrintf(SRN_LOG_INFO,  NULL, 0, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...)  SRN_LogPrintf(SRN_LOG_WARN,  NULL, 0, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...)  SRN_LogPrintf(SRN_LOG_ERROR, NULL, 0, fmt, ##__VA_ARGS__)
#define LOGD_SRC(src, fmt, ...)  SRN_LogPrintf(SRN_LOG_DEBUG, (src), 0, fmt, ##__VA_ARGS__)
#define LOGI_SRC(src, fmt, ...)  SRN_LogPrintf(SRN_LOG_INFO,  (src), 0, fmt, ##__VA_ARGS__)
#define LOGW_SRC(src, fmt, ...)  SRN_LogPrintf(SRN_LOG_WARN,  (src), 0, fmt, ##__VA_ARGS__)
#define LOGE_SRC(src, fmt, ...)  SRN_LogPrintf(SRN_LOG_ERROR, (src), 0, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif
