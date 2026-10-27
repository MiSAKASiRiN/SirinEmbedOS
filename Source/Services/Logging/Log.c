#include "Services/Logging/log_internal.h"
#include "SirinEmbedOS/IO/Serial/Serial.h"
#include "boot_diag.h"
#include <stdio.h>
#include <string.h>

SRNOS_DEBUG_WATCH_BSS volatile SRNOS_DbgLog SRNOS_DebugLog;

static osMessageQueueId_t s_log_q    = NULL; /* 实际使用的队列（存“指针”） */
static osMemoryPoolId_t   s_log_pool = NULL; /* 内存池（存消息块） */
static uint8_t            s_log_inited = 0u;
static SRNOS_LogUSBTransport s_usb_transport;
static SRNOS_SerialChannel *s_uart_channel;
static bool s_transports_frozen;

static uint32_t s_backend_mask =
#if SRNOS_LOG_ENABLE_USB_CDC
    SRNOS_LOG_BACKEND_USB_CDC
#else
    0u
#endif
#if SRNOS_LOG_ENABLE_SWO
    | SRNOS_LOG_BACKEND_SWO
#endif
#if SRNOS_LOG_ENABLE_UART
    | SRNOS_LOG_BACKEND_UART
#endif
    ;

enum {
    SRNOS_LOG_STATUS_OK = 0u,
    SRNOS_LOG_STATUS_QUEUE_DROP = 1u,
    SRNOS_LOG_STATUS_BACKEND_DROP = 2u,
    SRNOS_LOG_STATUS_TRUNCATED = 3u,
};

enum {
    SRNOS_LOG_BACKEND_STATUS_USB_OK = (1u << 0),
    SRNOS_LOG_BACKEND_STATUS_USB_DROP = (1u << 1),
    SRNOS_LOG_BACKEND_STATUS_SWO_OK = (1u << 2),
    SRNOS_LOG_BACKEND_STATUS_SWO_DROP = (1u << 3),
    SRNOS_LOG_BACKEND_STATUS_UART_OK = (1u << 4),
    SRNOS_LOG_BACKEND_STATUS_UART_DROP = (1u << 5),
};

static uint32_t SRNOS_AllowedBackendMask(void)
{
    uint32_t mask = 0u;
#if SRNOS_LOG_ENABLE_USB_CDC
    mask |= SRNOS_LOG_BACKEND_USB_CDC;
#endif
#if SRNOS_LOG_ENABLE_SWO
    mask |= SRNOS_LOG_BACKEND_SWO;
#endif
#if SRNOS_LOG_ENABLE_UART
    mask |= SRNOS_LOG_BACKEND_UART;
#endif
    return mask;
}

static uint32_t SRNOS_LevelMaskBit(SRNOS_LogLevel level)
{
    switch (level) {
    case SRNOS_LOG_DEBUG:
        return SRNOS_LOG_LEVEL_MASK_DEBUG;
    case SRNOS_LOG_INFO:
        return SRNOS_LOG_LEVEL_MASK_INFO;
    case SRNOS_LOG_WARN:
        return SRNOS_LOG_LEVEL_MASK_WARN;
    case SRNOS_LOG_ERROR:
        return SRNOS_LOG_LEVEL_MASK_ERROR;
    default:
        return 0u;
    }
}

static bool SRNOS_BackendAcceptsLevel(uint32_t level_mask,
                                      SRNOS_LogLevel level,
                                      bool boot_line)
{
    return boot_line || ((level_mask & SRNOS_LevelMaskBit(level)) != 0u);
}

void SRNOS_LogInit(void)
{
    if (s_log_inited != 0u) {
        return;
    }

    SRNOS_DbgZero(&SRNOS_DebugLog, sizeof(SRNOS_DebugLog));
    SRNOS_DbgInit(&SRNOS_DebugLog.h,
                 SRNOS_DBG_MODULE_LOG,
                 0u,
                 (uint16_t)sizeof(SRNOS_DebugLog));
    SRNOS_DebugLog.backend_mask = s_backend_mask;
    s_log_inited = 1u;
}

static void SRNOS_DbgTouch(uint32_t status)
{
    SRNOS_LogInit();
    SRNOS_DebugLog.backend_mask = s_backend_mask;
    SRNOS_DbgEnd(&SRNOS_DebugLog.h, HAL_GetTick(), status);
}

void SRNOS_LogBindQueue(osMessageQueueId_t q) { s_log_q = q; }
void SRNOS_LogBindPool (osMemoryPoolId_t   p) { s_log_pool = p; }

void SRNOS_LogSetBackendMask(uint32_t backend_mask)
{
    if (s_transports_frozen) return;
    SRNOS_LogInit();
    s_backend_mask = backend_mask & SRNOS_AllowedBackendMask();
    SRNOS_DbgTouch(SRNOS_LOG_STATUS_OK);
}

uint32_t SRNOS_LogGetBackendMask(void)
{
    SRNOS_LogInit();
    return s_backend_mask;
}

bool SRNOS_LogLevelEnabled(SRNOS_LogLevel level)
{
    const uint32_t mask = s_backend_mask;
    const uint32_t level_bit = SRNOS_LevelMaskBit(level);
    uint32_t enabled_levels = 0u;
#if SRNOS_LOG_ENABLE_USB_CDC
    if ((mask & SRNOS_LOG_BACKEND_USB_CDC) != 0u) {
        enabled_levels |= SRNOS_LOG_USB_LEVEL_MASK;
    }
#endif
#if SRNOS_LOG_ENABLE_SWO
    if ((mask & SRNOS_LOG_BACKEND_SWO) != 0u) {
        enabled_levels |= SRNOS_LOG_SWO_LEVEL_MASK;
    }
#endif
#if SRNOS_LOG_ENABLE_UART
    if ((mask & SRNOS_LOG_BACKEND_UART) != 0u) {
        enabled_levels |= SRNOS_LOG_UART_LEVEL_MASK;
    }
#endif
    (void)mask;
    return (enabled_levels & level_bit) != 0u;
}

void SRNOS_LogGetStats(SRNOS_LogStats* out)
{
    if (out == NULL) {
        return;
    }

    SRNOS_LogInit();
    out->queued = SRNOS_DebugLog.queued;
    out->printed = SRNOS_DebugLog.printed;
    out->dropped_queue = SRNOS_DebugLog.dropped_queue;
    out->dropped_backend = SRNOS_DebugLog.dropped_backend;
    out->truncated_body = SRNOS_DebugLog.truncated_body;
    out->direct_printed = SRNOS_DebugLog.direct_printed;
    out->uart_dma_error = SRNOS_DebugLog.uart_dma_error;
    out->uart_busy = SRNOS_DebugLog.uart_busy;
    out->uart_drop = SRNOS_DebugLog.uart_drop;
    out->swo_drop = SRNOS_DebugLog.swo_drop;
    out->backend_mask = SRNOS_DebugLog.backend_mask;
    out->last_line_len = SRNOS_DebugLog.last_line_len;
    out->last_backend_status = SRNOS_DebugLog.last_backend_status;
    out->rx_enabled = SRNOS_DebugLog.rx_enabled;
    out->rx_bytes = SRNOS_DebugLog.rx_bytes;
    out->rx_events = SRNOS_DebugLog.rx_events;
    out->rx_overflow_bytes = SRNOS_DebugLog.rx_overflow_bytes;
    out->rx_errors = SRNOS_DebugLog.rx_errors;
    out->last_rx_size = SRNOS_DebugLog.last_rx_size;
    out->usb_configured = SRNOS_DebugLog.usb_configured;
    out->usb_tx_bytes = SRNOS_DebugLog.usb_tx_bytes;
    out->usb_rx_bytes = SRNOS_DebugLog.usb_rx_bytes;
    out->usb_tx_drop = SRNOS_DebugLog.usb_tx_drop;
    out->usb_rx_drop = SRNOS_DebugLog.usb_rx_drop;
    out->usb_tx_busy = SRNOS_DebugLog.usb_tx_busy;
    out->usb_tx_errors = SRNOS_DebugLog.usb_tx_errors;
    out->usb_rx_overflow = SRNOS_DebugLog.usb_rx_overflow;
    out->usb_rx_pending = SRNOS_DebugLog.usb_rx_pending;
    out->uart_bytes = SRNOS_DebugLog.uart_bytes;
    out->uart_writes = SRNOS_DebugLog.uart_writes;
    out->uart_dropped_bytes = SRNOS_DebugLog.uart_dropped_bytes;
}

void SRNOS_LogRecordDroppedQueue(void)
{
    SRNOS_LogInit();
    SRNOS_DebugLog.dropped_queue++;
    SRNOS_DbgTouch(SRNOS_LOG_STATUS_QUEUE_DROP);
}

void SRNOS_LogRecordTruncatedBody(void)
{
    SRNOS_LogInit();
    SRNOS_DebugLog.truncated_body++;
    SRNOS_DbgTouch(SRNOS_LOG_STATUS_TRUNCATED);
}

void SRNOS_LogRegisterUsbTransport(const SRNOS_LogUSBTransport *transport)
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

void SRNOS_LogServiceUsbTransport(void)
{
    if (s_usb_transport.service != NULL) {
        s_usb_transport.service(s_usb_transport.user);
    }
}

static inline const char* SRNOS_GetSource(const char* user_src)
{
    if (user_src && user_src[0]) return user_src;
    if (xPortIsInsideInterrupt() == pdTRUE) return "System";
    TaskHandle_t h = xTaskGetCurrentTaskHandle();
    const char* nm = h ? pcTaskGetName(h) : NULL;
    return (nm && nm[0]) ? nm : "System";
}

static inline const char* SRNOS_LvlStr(SRNOS_LogLevel lvl)
{
    switch (lvl) {
        case SRNOS_LOG_DEBUG: return "DBUG";
        case SRNOS_LOG_INFO:  return "INFO";
        case SRNOS_LOG_WARN:  return "WARN";
        case SRNOS_LOG_ERROR: return "EROR";
        default:            return "UNKN";
    }
}

static void SRNOS_FmtTsMs(char* out, size_t out_sz, uint64_t ms64)
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
static inline uint64_t SRNOS_CaptureMs64(void)
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

size_t SRNOS_LogFormatLine(char* out,
                         size_t out_sz,
                         const SRNOS_LogMsg* msg,
                         bool* truncated)
{
    if (truncated != NULL) {
        *truncated = false;
    }
    if (out == NULL || out_sz == 0u || msg == NULL) {
        return 0u;
    }

    char tsbuf[20];
    SRNOS_FmtTsMs(tsbuf, sizeof(tsbuf), msg->ts_ms);

    int n = snprintf(out,
                     out_sz,
                     "%s[%s][%s] %s\r\n",
                     tsbuf,
                     SRNOS_LvlStr(msg->level),
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

#if SRNOS_LOG_ENABLE_SWO
static bool SRNOS_SwoWriteLine(const char* line, size_t len)
{
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
}
#endif

static bool log_uart_write(const char *line, size_t len)
{
#if SRNOS_LOG_ENABLE_UART
    if ((line == NULL) || (len == 0u)) {
        return false;
    }

    SRNOS_LogInit();
    const size_t written = (s_uart_channel != NULL &&
        SRNOS_SerialWrite(s_uart_channel, line, len, 0u) == SRNOS_SERIAL_OK) ? len : 0u;

    if (written == len) {
        SRNOS_DebugLog.uart_bytes += (uint32_t)written;
        SRNOS_DebugLog.uart_writes++;
        return true;
    }

    SRNOS_DebugLog.uart_bytes += (uint32_t)written;
    SRNOS_DebugLog.uart_dropped_bytes += (uint32_t)(len - written);
    return false;
#else
    (void)line;
    (void)len;
    return false;
#endif
}

SRNOS_LogOutputStatus SRNOS_LogOutputLineEx(const char* line,
                                            size_t len,
                                            SRNOS_LogLevel level,
                                            bool boot_line)
{
    if (boot_line && line && len) SRNOS_LogCaptureBoot(line, len);
    if (line == NULL || len == 0u) {
        return SRNOS_LOG_OUTPUT_INVALID;
    }

    /* Configuration callbacks may log before the FreeRTOS port has reset its
     * initial critical-nesting sentinel. Do not enter a queue/USB/Serial sink
     * here; retain startup text without touching RTOS critical sections. */
    if (osKernelGetState() != osKernelRunning) {
        if (!boot_line) SRNOS_LogCaptureBoot(line, len);
        return SRNOS_LOG_OUTPUT_NO_BACKEND;
    }

    SRNOS_LogInit();

    const uint32_t mask = s_backend_mask;
    uint32_t backend_status = 0u;
    bool accepted = false;

#if SRNOS_LOG_ENABLE_USB_CDC
    if (((mask & SRNOS_LOG_BACKEND_USB_CDC) != 0u) &&
        SRNOS_BackendAcceptsLevel(SRNOS_LOG_USB_LEVEL_MASK, level, boot_line)) {
        if ((s_usb_transport.submit != NULL) &&
            s_usb_transport.submit(s_usb_transport.user,
                                   (const uint8_t *)line,
                                   len)) {
            backend_status |= SRNOS_LOG_BACKEND_STATUS_USB_OK;
            accepted = true;
        } else {
            backend_status |= SRNOS_LOG_BACKEND_STATUS_USB_DROP;
        }
    }
#endif

#if SRNOS_LOG_ENABLE_SWO
    if (((mask & SRNOS_LOG_BACKEND_SWO) != 0u) &&
        SRNOS_BackendAcceptsLevel(SRNOS_LOG_SWO_LEVEL_MASK, level, boot_line)) {
        if (SRNOS_SwoWriteLine(line, len)) {
            backend_status |= SRNOS_LOG_BACKEND_STATUS_SWO_OK;
            accepted = true;
        } else {
            backend_status |= SRNOS_LOG_BACKEND_STATUS_SWO_DROP;
            SRNOS_DebugLog.swo_drop++;
        }
    }
#endif

#if SRNOS_LOG_ENABLE_UART
    if (((mask & SRNOS_LOG_BACKEND_UART) != 0u) &&
        SRNOS_BackendAcceptsLevel(SRNOS_LOG_UART_LEVEL_MASK, level, boot_line)) {
        if (log_uart_write(line, len)) {
            backend_status |= SRNOS_LOG_BACKEND_STATUS_UART_OK;
            accepted = true;
        } else {
            backend_status |= SRNOS_LOG_BACKEND_STATUS_UART_DROP;
        }
    }
#endif

    SRNOS_DebugLog.last_line_len = (uint32_t)len;
    SRNOS_DebugLog.last_backend_status = backend_status;

    if (accepted) {
        SRNOS_DebugLog.printed++;
        SRNOS_DbgTouch(SRNOS_LOG_STATUS_OK);
        return SRNOS_LOG_OUTPUT_OK;
    }

    SRNOS_DebugLog.dropped_backend++;
    SRNOS_DbgTouch(SRNOS_LOG_STATUS_BACKEND_DROP);
    return (mask == 0u) ? SRNOS_LOG_OUTPUT_NO_BACKEND : SRNOS_LOG_OUTPUT_BACKEND_FULL;
}

SRNOS_LogOutputStatus SRNOS_LogOutputLine(const char* line, size_t len)
{
    return SRNOS_LogOutputLineEx(line, len, SRNOS_LOG_INFO, false);
}

static void SRNOS_FillMessage(SRNOS_LogMsg* msg,
                             SRNOS_LogLevel level,
                             const char* source,
                             const char* fmt,
                             va_list ap)
{
    memset(msg, 0, sizeof(*msg));
    msg->ts_ms = SRNOS_CaptureMs64();
    msg->level = level;
    const char* src = SRNOS_GetSource(source);
    strncpy(msg->source, src, SRNOS_LOG_SRC_MAX - 1u);
    msg->source[SRNOS_LOG_SRC_MAX - 1u] = '\0';

    int n = vsnprintf(msg->body, SRNOS_LOG_BODY_MAX, fmt, ap);
    if (n < 0) {
        msg->body[0] = '\0';
        SRNOS_LogRecordTruncatedBody();
    } else if ((size_t)n >= SRNOS_LOG_BODY_MAX) {
        msg->body[SRNOS_LOG_BODY_MAX - 1u] = '\0';
        SRNOS_LogRecordTruncatedBody();
    }
}

static SRNOS_LogResult capture_startup_line(SRNOS_LogLevel level,
                                        const char* source,
                                        const char* fmt,
                                        va_list ap)
{
    SRNOS_LogMsg msg;
    SRNOS_FillMessage(&msg, level, source, fmt, ap);

    char line[SRNOS_LOG_LINE_MAX];
    bool truncated = false;
    size_t len = SRNOS_LogFormatLine(line, sizeof(line), &msg, &truncated);
    if (truncated) {
        SRNOS_LogRecordTruncatedBody();
    }

    SRNOS_LogCaptureBoot(line,len);
    /* Preserve the startup result: no running queue/backend has accepted it. */
    return SRNOS_LOG_FULL;
}

static bool try_drop_oldest(void)
{
    uintptr_t pv_old;
    if (osMessageQueueGet(s_log_q, &pv_old, NULL, 0) == osOK) {
        osMemoryPoolFree(s_log_pool, (void*)pv_old);
        SRNOS_LogRecordDroppedQueue();
        return true;
    }
    return false;
}

SRNOS_LogResult SRNOS_LogVPrintf(SRNOS_LogLevel level,
                          const char* source,
                          const char* fmt,
                          va_list ap)
{
    if(fmt==NULL || level<SRNOS_LOG_DEBUG || level>SRNOS_LOG_ERROR) return SRNOS_LOG_INVALID;
    if(xPortIsInsideInterrupt()==pdTRUE) return SRNOS_LOG_ISR_FORBIDDEN;
    SRNOS_LogInit();

    /* Pre-scheduler diagnostics only retain text in the existing boot buffer.
     * Running applications never enter this path because queue/pool setup fails. */
    if(osKernelGetState()!=osKernelRunning && osKernelGetState()!=osKernelLocked) {
        va_list copy;va_copy(copy,ap);
        const SRNOS_LogResult result=capture_startup_line(level,source,fmt,copy);
        va_end(copy);return result;
    }
    if(s_log_q==NULL || s_log_pool==NULL) {
        SRNOS_LogRecordDroppedQueue();return SRNOS_LOG_FULL;
    }

    SRNOS_LogMsg* node = (SRNOS_LogMsg*)osMemoryPoolAlloc(s_log_pool, 0);
    if (node == NULL) {
        (void)try_drop_oldest();
        node = (SRNOS_LogMsg*)osMemoryPoolAlloc(s_log_pool, 0);
        if (node == NULL) {
            SRNOS_LogRecordDroppedQueue();
            return SRNOS_LOG_NO_MEMORY;
        }
    }

    SRNOS_FillMessage(node, level, source, fmt, ap);

    uintptr_t ptr = (uintptr_t)node;
    osStatus_t st = osMessageQueuePut(s_log_q, &ptr, 0, 0);
    if (st == osOK) {
        SRNOS_DebugLog.queued++;
        SRNOS_DbgTouch(SRNOS_LOG_STATUS_OK);
        return SRNOS_LOG_OK;
    }

    (void)try_drop_oldest();
    st = osMessageQueuePut(s_log_q, &ptr, 0, 0);
    if (st == osOK) {
        SRNOS_DebugLog.queued++;
        SRNOS_DbgTouch(SRNOS_LOG_STATUS_OK);
        return SRNOS_LOG_OK;
    }

    osMemoryPoolFree(s_log_pool, node);
    SRNOS_LogRecordDroppedQueue();
    return st==osErrorResource ? SRNOS_LOG_FULL : SRNOS_LOG_ERROR_RESULT;
}

SRNOS_LogResult SRNOS_LogPrintf(SRNOS_LogLevel level,
                         const char* source,
                         const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    SRNOS_LogResult st = SRNOS_LogVPrintf(level, source, fmt, ap);
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
