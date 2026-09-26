#include "SirinEmbedOS/Services/Logging/bsp_srn_log.h"
#include "SirinEmbedOS/IO/Serial/Serial.h"
#include "omx_boot_diag.h"
#include <stdio.h>
#include <string.h>

OMX_DEBUG_WATCH_BSS volatile omx_dbg_srn_log_t g_omx_dbg_srn_log;

static osMessageQueueId_t s_log_q    = NULL; /* 实际使用的队列（存“指针”） */
static osMemoryPoolId_t   s_log_pool = NULL; /* 内存池（存消息块） */
static uint8_t            s_log_inited = 0u;
static srn_log_usb_transport_t s_usb_transport;
static SRNOS_SerialChannel *s_uart_channel;
static bool s_transports_frozen;

static uint32_t s_backend_mask =
#if SRN_LOG_ENABLE_USB_CDC
    SRN_LOG_BACKEND_USB_CDC
#else
    0u
#endif
#if SRN_LOG_ENABLE_SWO
    | SRN_LOG_BACKEND_SWO
#endif
#if SRN_LOG_ENABLE_USART1_FALLBACK
    | SRN_LOG_BACKEND_USART1_FALLBACK
#endif
    ;

enum {
    SRN_LOG_STATUS_OK = 0u,
    SRN_LOG_STATUS_QUEUE_DROP = 1u,
    SRN_LOG_STATUS_BACKEND_DROP = 2u,
    SRN_LOG_STATUS_TRUNCATED = 3u,
    SRN_LOG_STATUS_UART_ERROR = 4u,
};

enum {
    SRN_LOG_BACKEND_STATUS_USB_OK = (1u << 0),
    SRN_LOG_BACKEND_STATUS_USB_DROP = (1u << 1),
    SRN_LOG_BACKEND_STATUS_SWO_OK = (1u << 2),
    SRN_LOG_BACKEND_STATUS_SWO_DROP = (1u << 3),
    SRN_LOG_BACKEND_STATUS_UART_FALLBACK_OK = (1u << 4),
    SRN_LOG_BACKEND_STATUS_UART_FALLBACK_DROP = (1u << 5),
};

static uint32_t srn_allowed_backend_mask(void)
{
    uint32_t mask = 0u;
#if SRN_LOG_ENABLE_USB_CDC
    mask |= SRN_LOG_BACKEND_USB_CDC;
#endif
#if SRN_LOG_ENABLE_SWO
    mask |= SRN_LOG_BACKEND_SWO;
#endif
#if SRN_LOG_ENABLE_USART1_FALLBACK
    mask |= SRN_LOG_BACKEND_USART1_FALLBACK;
#endif
    return mask;
}

static uint32_t srn_level_mask_bit(srn_log_level_t level)
{
    switch (level) {
    case SRN_LOG_DEBUG:
        return SRN_LOG_LEVEL_MASK_DEBUG;
    case SRN_LOG_INFO:
        return SRN_LOG_LEVEL_MASK_INFO;
    case SRN_LOG_WARN:
        return SRN_LOG_LEVEL_MASK_WARN;
    case SRN_LOG_ERROR:
        return SRN_LOG_LEVEL_MASK_ERROR;
    default:
        return 0u;
    }
}

static bool srn_backend_accepts_level(uint32_t level_mask,
                                      srn_log_level_t level,
                                      bool boot_line)
{
    return boot_line || ((level_mask & srn_level_mask_bit(level)) != 0u);
}

void SRN_Log_Init(void)
{
    if (s_log_inited != 0u) {
        return;
    }

    omx_dbg_zero(&g_omx_dbg_srn_log, sizeof(g_omx_dbg_srn_log));
    omx_dbg_init(&g_omx_dbg_srn_log.h,
                 OMX_DBG_MODULE_SRN_LOG,
                 0u,
                 (uint16_t)sizeof(g_omx_dbg_srn_log));
    g_omx_dbg_srn_log.backend_mask = s_backend_mask;
    s_log_inited = 1u;
}

static void srn_dbg_touch(uint32_t status)
{
    SRN_Log_Init();
    g_omx_dbg_srn_log.backend_mask = s_backend_mask;
    omx_dbg_end(&g_omx_dbg_srn_log.h, HAL_GetTick(), status);
}

void SRN_Log_BindQueue(osMessageQueueId_t q) { s_log_q = q; }
void SRN_Log_BindPool (osMemoryPoolId_t   p) { s_log_pool = p; }

void SRN_Log_SetBackendMask(uint32_t backend_mask)
{
    if (s_transports_frozen) return;
    SRN_Log_Init();
    s_backend_mask = backend_mask & srn_allowed_backend_mask();
    srn_dbg_touch(SRN_LOG_STATUS_OK);
}

uint32_t SRN_Log_GetBackendMask(void)
{
    SRN_Log_Init();
    return s_backend_mask;
}

void SRN_Log_GetStats(srn_log_stats_t* out)
{
    if (out == NULL) {
        return;
    }

    SRN_Log_Init();
    out->queued = g_omx_dbg_srn_log.queued;
    out->printed = g_omx_dbg_srn_log.printed;
    out->dropped_queue = g_omx_dbg_srn_log.dropped_queue;
    out->dropped_backend = g_omx_dbg_srn_log.dropped_backend;
    out->truncated_body = g_omx_dbg_srn_log.truncated_body;
    out->direct_printed = g_omx_dbg_srn_log.direct_printed;
    out->uart_dma_error = g_omx_dbg_srn_log.uart_dma_error;
    out->uart_busy = g_omx_dbg_srn_log.uart_busy;
    out->uart_drop = g_omx_dbg_srn_log.uart_drop;
    out->swo_drop = g_omx_dbg_srn_log.swo_drop;
    out->backend_mask = g_omx_dbg_srn_log.backend_mask;
    out->last_line_len = g_omx_dbg_srn_log.last_line_len;
    out->last_backend_status = g_omx_dbg_srn_log.last_backend_status;
    out->rx_enabled = g_omx_dbg_srn_log.rx_enabled;
    out->rx_bytes = g_omx_dbg_srn_log.rx_bytes;
    out->rx_events = g_omx_dbg_srn_log.rx_events;
    out->rx_overflow_bytes = g_omx_dbg_srn_log.rx_overflow_bytes;
    out->rx_errors = g_omx_dbg_srn_log.rx_errors;
    out->last_rx_size = g_omx_dbg_srn_log.last_rx_size;
    out->usb_configured = g_omx_dbg_srn_log.usb_configured;
    out->usb_tx_bytes = g_omx_dbg_srn_log.usb_tx_bytes;
    out->usb_rx_bytes = g_omx_dbg_srn_log.usb_rx_bytes;
    out->usb_tx_drop = g_omx_dbg_srn_log.usb_tx_drop;
    out->usb_rx_drop = g_omx_dbg_srn_log.usb_rx_drop;
    out->usb_tx_busy = g_omx_dbg_srn_log.usb_tx_busy;
    out->usb_tx_errors = g_omx_dbg_srn_log.usb_tx_errors;
    out->usb_rx_overflow = g_omx_dbg_srn_log.usb_rx_overflow;
    out->usb_rx_pending = g_omx_dbg_srn_log.usb_rx_pending;
    out->uart_fallback_bytes = g_omx_dbg_srn_log.uart_fallback_bytes;
    out->uart_fallback_writes = g_omx_dbg_srn_log.uart_fallback_writes;
    out->uart_fallback_drop = g_omx_dbg_srn_log.uart_fallback_drop;
}

void SRN_Log_RecordDroppedQueue(void)
{
    SRN_Log_Init();
    g_omx_dbg_srn_log.dropped_queue++;
    srn_dbg_touch(SRN_LOG_STATUS_QUEUE_DROP);
}

void SRN_Log_RecordTruncatedBody(void)
{
    SRN_Log_Init();
    g_omx_dbg_srn_log.truncated_body++;
    srn_dbg_touch(SRN_LOG_STATUS_TRUNCATED);
}

void SRN_Log_RecordUartDmaError(void)
{
    SRN_Log_Init();
    g_omx_dbg_srn_log.uart_dma_error++;
    srn_dbg_touch(SRN_LOG_STATUS_UART_ERROR);
}

void SRN_Log_RecordUartBusy(void)
{
    SRN_Log_Init();
    g_omx_dbg_srn_log.uart_busy++;
    srn_dbg_touch(SRN_LOG_STATUS_UART_ERROR);
}

void SRN_Log_RecordUartDrop(void)
{
    SRN_Log_Init();
    g_omx_dbg_srn_log.uart_drop++;
    srn_dbg_touch(SRN_LOG_STATUS_BACKEND_DROP);
}

void SRN_Log_RegisterUsbTransport(const srn_log_usb_transport_t *transport)
{
    if (s_transports_frozen) return;
    if (transport == NULL) {
        s_usb_transport.user = NULL;
        s_usb_transport.submit = NULL;
        s_usb_transport.service = NULL;
        return;
    }
    s_usb_transport.user = transport->user;
    s_usb_transport.submit = transport->submit;
    s_usb_transport.service = transport->service;
}

void SRN_Log_ServiceUsbTransport(void)
{
    if (s_usb_transport.service != NULL) {
        s_usb_transport.service(s_usb_transport.user);
    }
}

static inline const char* srn_get_source(const char* user_src)
{
    if (user_src && user_src[0]) return user_src;
    if (xPortIsInsideInterrupt() == pdTRUE) return "System";
    TaskHandle_t h = xTaskGetCurrentTaskHandle();
    const char* nm = h ? pcTaskGetName(h) : NULL;
    return (nm && nm[0]) ? nm : "System";
}

static inline const char* srn_lvl_str(srn_log_level_t lvl)
{
    switch (lvl) {
        case SRN_LOG_DEBUG: return "DBUG";
        case SRN_LOG_INFO:  return "INFO";
        case SRN_LOG_WARN:  return "WARN";
        case SRN_LOG_ERROR: return "EROR";
        default:            return "UNKN";
    }
}

static void srn_fmt_ts_ms(char* out, size_t out_sz, uint64_t ms64)
{
    uint32_t sec = (uint32_t)(ms64 / 1000ULL);
    uint32_t ms  = (uint32_t)(ms64 % 1000ULL);
    if (sec <= 999999U) {
        (void)snprintf(out, out_sz, "[%6lu.%03lu]", (unsigned long)sec, (unsigned long)ms);
    } else {
        (void)snprintf(out, out_sz, "[%lu.%03lu]",  (unsigned long)sec, (unsigned long)ms);
    }
}

/* HAL_GetTick() 的 32 位值扩展为 64 位单调毫秒（49天回绕→64位高位计数） */
static inline uint64_t srn_capture_ms64(void)
{
    static uint32_t last32 = 0;
    static uint64_t hi     = 0;
    const uint32_t mask = __get_PRIMASK();
    __disable_irq();
    uint32_t cur = HAL_GetTick();
    if (cur < last32) hi += (1ULL << 32);
    last32 = cur;
    uint64_t ms64 = hi | (uint64_t)cur;
    __set_PRIMASK(mask);
    return ms64;
}

size_t SRN_LogFormatLine(char* out,
                         size_t out_sz,
                         const srn_log_msg_t* msg,
                         bool* truncated)
{
    if (truncated != NULL) {
        *truncated = false;
    }
    if (out == NULL || out_sz == 0u || msg == NULL) {
        return 0u;
    }

    char tsbuf[20];
    srn_fmt_ts_ms(tsbuf, sizeof(tsbuf), msg->ts_ms);

    int n = snprintf(out,
                     out_sz,
                     "%s[%s][%s] %s\r\n",
                     tsbuf,
                     srn_lvl_str(msg->level),
                     msg->source,
                     msg->body);
    if (n < 0) {
        out[0] = '\0';
        if (truncated != NULL) {
            *truncated = true;
        }
        return 0u;
    }

    if ((size_t)n < out_sz) {
        return (size_t)n;
    }

    if (truncated != NULL) {
        *truncated = true;
    }
    if (out_sz >= 3u) {
        out[out_sz - 3u] = '\r';
        out[out_sz - 2u] = '\n';
        out[out_sz - 1u] = '\0';
        return out_sz - 1u;
    }

    out[out_sz - 1u] = '\0';
    return out_sz - 1u;
}

static bool srn_swo_write_line(const char* line, size_t len)
{
#if SRN_LOG_ENABLE_SWO
    if (line == NULL || len == 0u) {
        return false;
    }
    if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0u) {
        return false;
    }
    if (((ITM->TCR & ITM_TCR_ITMENA_Msk) == 0u) || ((ITM->TER & 1u) == 0u)) {
        return false;
    }

    for (size_t i = 0u; i < len; ++i) {
        if (ITM->PORT[0u].u32 == 0u) {
            return false;
        }
        ITM->PORT[0u].u8 = (uint8_t)line[i];
    }
    return true;
#else
    (void)line;
    (void)len;
    return false;
#endif
}

static bool srn_uart_fallback_write(const char *line, size_t len)
{
#if SRN_LOG_ENABLE_USART1_FALLBACK
    if ((line == NULL) || (len == 0u)) {
        return false;
    }

    SRN_Log_Init();
    const size_t written = (s_uart_channel != NULL &&
        SRNOS_SerialWrite(s_uart_channel, line, len, 0u) == SRNOS_SERIAL_OK) ? len : 0u;

    if (written == len) {
        g_omx_dbg_srn_log.uart_fallback_bytes += (uint32_t)written;
        g_omx_dbg_srn_log.uart_fallback_writes++;
        return true;
    }

    g_omx_dbg_srn_log.uart_fallback_bytes += (uint32_t)written;
    g_omx_dbg_srn_log.uart_fallback_drop += (uint32_t)(len - written);
    return false;
#else
    (void)line;
    (void)len;
    return false;
#endif
}

void SRN_Log_WriteBootFallback(const char* line)
{
    if (line == NULL) {
        return;
    }
    if ((s_backend_mask & SRN_LOG_BACKEND_USART1_FALLBACK) == 0u) {
        return;
    }
    size_t len = 0u;
    while (line[len] != '\0') {
        ++len;
    }
    (void)srn_uart_fallback_write(line, len);
}

srn_log_output_status_t SRN_LogOutputLineEx(const char* line,
                                            size_t len,
                                            srn_log_level_t level,
                                            bool boot_line)
{
    if (boot_line && line && len) SRNOS_LogCaptureBoot(line, len);
    if (line == NULL || len == 0u) {
        return SRN_LOG_OUTPUT_INVALID;
    }

    /* Configuration callbacks may log before the FreeRTOS port has reset its
     * initial critical-nesting sentinel. Do not enter a queue/USB/Serial sink
     * here; retain startup text without touching RTOS critical sections. */
    if (osKernelGetState() != osKernelRunning) {
        if (!boot_line) SRNOS_LogCaptureBoot(line, len);
        return SRN_LOG_OUTPUT_NO_BACKEND;
    }

    SRN_Log_Init();

    const uint32_t mask = s_backend_mask;
    uint32_t backend_status = 0u;
    bool accepted = false;

#if SRN_LOG_ENABLE_USB_CDC
    if (((mask & SRN_LOG_BACKEND_USB_CDC) != 0u) &&
        srn_backend_accepts_level(SRN_LOG_USB_LEVEL_MASK, level, boot_line)) {
        if ((s_usb_transport.submit != NULL) &&
            s_usb_transport.submit(s_usb_transport.user,
                                   (const uint8_t *)line,
                                   len)) {
            backend_status |= SRN_LOG_BACKEND_STATUS_USB_OK;
            accepted = true;
        } else {
            backend_status |= SRN_LOG_BACKEND_STATUS_USB_DROP;
        }
    }
#endif

#if SRN_LOG_ENABLE_SWO
    if (((mask & SRN_LOG_BACKEND_SWO) != 0u) &&
        srn_backend_accepts_level(SRN_LOG_SWO_LEVEL_MASK, level, boot_line)) {
        if (srn_swo_write_line(line, len)) {
            backend_status |= SRN_LOG_BACKEND_STATUS_SWO_OK;
            accepted = true;
        } else {
            backend_status |= SRN_LOG_BACKEND_STATUS_SWO_DROP;
            g_omx_dbg_srn_log.swo_drop++;
        }
    }
#endif

#if SRN_LOG_ENABLE_USART1_FALLBACK
    if (((mask & SRN_LOG_BACKEND_USART1_FALLBACK) != 0u) &&
        srn_backend_accepts_level(SRN_LOG_USART1_LEVEL_MASK, level, boot_line)) {
        if (srn_uart_fallback_write(line, len)) {
            backend_status |= SRN_LOG_BACKEND_STATUS_UART_FALLBACK_OK;
            accepted = true;
        } else {
            backend_status |= SRN_LOG_BACKEND_STATUS_UART_FALLBACK_DROP;
        }
    }
#endif

    g_omx_dbg_srn_log.last_line_len = (uint32_t)len;
    g_omx_dbg_srn_log.last_backend_status = backend_status;

    if (accepted) {
        g_omx_dbg_srn_log.printed++;
        srn_dbg_touch(SRN_LOG_STATUS_OK);
        return SRN_LOG_OUTPUT_OK;
    }

    g_omx_dbg_srn_log.dropped_backend++;
    srn_dbg_touch(SRN_LOG_STATUS_BACKEND_DROP);
    return (mask == 0u) ? SRN_LOG_OUTPUT_NO_BACKEND : SRN_LOG_OUTPUT_BACKEND_FULL;
}

srn_log_output_status_t SRN_LogOutputLine(const char* line, size_t len)
{
    return SRN_LogOutputLineEx(line, len, SRN_LOG_INFO, false);
}

static void srn_fill_message(srn_log_msg_t* msg,
                             srn_log_level_t level,
                             const char* source,
                             const char* fmt,
                             va_list ap)
{
    memset(msg, 0, sizeof(*msg));
    msg->ts_ms = srn_capture_ms64();
    msg->level = level;
    const char* src = srn_get_source(source);
    strncpy(msg->source, src, SRN_LOG_SRC_MAX - 1u);
    msg->source[SRN_LOG_SRC_MAX - 1u] = '\0';

    int n = vsnprintf(msg->body, SRN_LOG_BODY_MAX, fmt, ap);
    if (n < 0) {
        msg->body[0] = '\0';
        SRN_Log_RecordTruncatedBody();
    } else if ((size_t)n >= SRN_LOG_BODY_MAX) {
        msg->body[SRN_LOG_BODY_MAX - 1u] = '\0';
        SRN_Log_RecordTruncatedBody();
    }
}

static osStatus_t srn_direct_print_line(srn_log_level_t level,
                                        const char* source,
                                        const char* fmt,
                                        va_list ap)
{
    srn_log_msg_t msg;
    srn_fill_message(&msg, level, source, fmt, ap);

    char line[SRN_LOG_LINE_MAX];
    bool truncated = false;
    size_t len = SRN_LogFormatLine(line, sizeof(line), &msg, &truncated);
    if (truncated) {
        SRN_Log_RecordTruncatedBody();
    }

    srn_log_output_status_t out_st =
        SRN_LogOutputLineEx(line, len, msg.level, false);
    if (out_st == SRN_LOG_OUTPUT_OK) {
        g_omx_dbg_srn_log.direct_printed++;
        srn_dbg_touch(SRN_LOG_STATUS_OK);
        return osOK;
    }
    return osErrorResource;
}

static bool srn_drop_oldest_locked(void)
{
    uintptr_t pv_old;
    if (osMessageQueueGet(s_log_q, &pv_old, NULL, 0) == osOK) {
        osMemoryPoolFree(s_log_pool, (void*)pv_old);
        SRN_Log_RecordDroppedQueue();
        return true;
    }
    return false;
}

osStatus_t SRN_LogVPrintf(srn_log_level_t level,
                          const char* source,
                          uint32_t timeout_ticks,
                          const char* fmt,
                          va_list ap)
{
    (void)timeout_ticks;
    SRN_Log_Init();

    if (fmt == NULL) {
        return osErrorParameter;
    }

    /* 禁止在 ISR 里放队列（有需要请改为任务里打印） */
    if (xPortIsInsideInterrupt() == pdTRUE) {
        return osErrorISR;
    }

    /* 队列或内存池未就绪：兜底直出完整一行 */
    if (osKernelGetState() != osKernelRunning || s_log_q == NULL || s_log_pool == NULL) {
        va_list ap2;
        va_copy(ap2, ap);
        osStatus_t st = srn_direct_print_line(level, source, fmt, ap2);
        va_end(ap2);
        return st;
    }

    srn_log_msg_t* node = (srn_log_msg_t*)osMemoryPoolAlloc(s_log_pool, 0);
    if (node == NULL) {
        (void)srn_drop_oldest_locked();
        node = (srn_log_msg_t*)osMemoryPoolAlloc(s_log_pool, 0);
        if (node == NULL) {
            va_list ap2;
            va_copy(ap2, ap);
            osStatus_t st = srn_direct_print_line(level, source, fmt, ap2);
            va_end(ap2);
            return (st == osOK) ? osErrorNoMemory : st;
        }
    }

    srn_fill_message(node, level, source, fmt, ap);

    uintptr_t ptr = (uintptr_t)node;
    osStatus_t st = osMessageQueuePut(s_log_q, &ptr, 0, 0);
    if (st == osOK) {
        g_omx_dbg_srn_log.queued++;
        srn_dbg_touch(SRN_LOG_STATUS_OK);
        return osOK;
    }

    (void)srn_drop_oldest_locked();
    st = osMessageQueuePut(s_log_q, &ptr, 0, 0);
    if (st == osOK) {
        g_omx_dbg_srn_log.queued++;
        srn_dbg_touch(SRN_LOG_STATUS_OK);
        return osOK;
    }

    osMemoryPoolFree(s_log_pool, node);
    SRN_Log_RecordDroppedQueue();
    return st;
}

osStatus_t SRN_LogPrintf(srn_log_level_t level,
                         const char* source,
                         uint32_t timeout_ticks,
                         const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    osStatus_t st = SRN_LogVPrintf(level, source, timeout_ticks, fmt, ap);
    va_end(ap);
    return st;
}

bool SRNOS_LogBindUart(SRNOS_SerialChannel *channel)
{
    if (s_transports_frozen) return false;
    if (channel != NULL && SRNOS_SerialClaim(channel, &s_uart_channel) != SRNOS_SERIAL_OK) return false;
    s_uart_channel = channel;
    return true;
}
void SRNOS_LogFreezeTransports(void) { s_transports_frozen = true; }
