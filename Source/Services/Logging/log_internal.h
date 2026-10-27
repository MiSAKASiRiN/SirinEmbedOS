#pragma once
#include "SirinEmbedOS/Services/Logging/Log.h"

#include "stm32h7xx_hal.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "SirinEmbedOS/Diagnostics/Watch/Watch.h"
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SRNOS_LOG_ENABLE_USB_CDC
#define SRNOS_LOG_ENABLE_USB_CDC           1
#endif
#ifndef SRNOS_LOG_ENABLE_SWO
#define SRNOS_LOG_ENABLE_SWO               0
#endif
#ifndef SRNOS_LOG_ENABLE_UART
#define SRNOS_LOG_ENABLE_UART   1
#endif
#ifndef SRNOS_LOG_USB_LEVEL_MASK
#define SRNOS_LOG_USB_LEVEL_MASK           SRNOS_LOG_LEVEL_MASK_ALL
#endif
#ifndef SRNOS_LOG_SWO_LEVEL_MASK
#define SRNOS_LOG_SWO_LEVEL_MASK           SRNOS_LOG_LEVEL_MASK_ALL
#endif
#ifndef SRNOS_LOG_UART_LEVEL_MASK
#define SRNOS_LOG_UART_LEVEL_MASK        SRNOS_LOG_LEVEL_MASK_ALL
#endif

typedef enum {
    SRNOS_LOG_OUTPUT_OK = 0,
    SRNOS_LOG_OUTPUT_INVALID,
    SRNOS_LOG_OUTPUT_NO_BACKEND,
    SRNOS_LOG_OUTPUT_BACKEND_FULL,
} SRNOS_LogOutputStatus;

typedef bool (*SRNOS_LogUSBSubmitFn)(void *user,
                                      const uint8_t *data,
                                      size_t length);
typedef void (*SRNOS_LogUSBServiceFn)(void *user);

typedef struct {
    void *user;
    SRNOS_LogUSBSubmitFn submit;
    SRNOS_LogUSBServiceFn service;
} SRNOS_LogUSBTransport;

/* 日志消息内容（真正的数据块，存放在 MemoryPool 的一个 block 里） */
typedef struct {
    uint64_t        ts_ms;               /* 64-bit 单调毫秒（HAL_GetTick 扩展） */
    SRNOS_LogLevel level;
    char            source[SRNOS_LOG_SRC_MAX];
    char            body[SRNOS_LOG_BODY_MAX];
    uint8_t         _pad[3];             /* 对齐 */
} SRNOS_LogMsg;

struct SRNOS_SerialChannel;
bool SRNOS_LogBindUart(struct SRNOS_SerialChannel *channel);
void SRNOS_LogFreezeTransports(void);

/* 初始化（保留扩展；当前仅毫秒时间基，无 DWT） */
void SRNOS_LogInit(void);

/* SDK startup binds its fixed-capacity queue and message pool. */
void SRNOS_LogBindQueue(osMessageQueueId_t q);
void SRNOS_LogBindPool (osMemoryPoolId_t   p);

void SRNOS_LogSetBackendMask(uint32_t backend_mask);
uint32_t SRNOS_LogGetBackendMask(void);
/* True when a compiled, selected backend allows this level. This does not
 * promise queue capacity or physical transport readiness/delivery. */
bool SRNOS_LogLevelEnabled(SRNOS_LogLevel level);
void SRNOS_LogGetStats(SRNOS_LogStats* out);
void SRNOS_LogRecordDroppedQueue(void);
void SRNOS_LogRecordTruncatedBody(void);

/* Project-owned ASCII USB transport. submit() must copy before returning. */
void SRNOS_LogRegisterUsbTransport(const SRNOS_LogUSBTransport *transport);
void SRNOS_LogServiceUsbTransport(void);

size_t SRNOS_LogFormatLine(char* out,
                         size_t out_sz,
                         const SRNOS_LogMsg* msg,
                         bool* truncated);
SRNOS_LogOutputStatus SRNOS_LogOutputLine(const char* line, size_t len);
SRNOS_LogOutputStatus SRNOS_LogOutputLineEx(const char* line,
                                            size_t len,
                                            SRNOS_LogLevel level,
                                            bool boot_line);

#ifdef __cplusplus
}
#endif
