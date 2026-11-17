#include "thread_internal.h"
#include "Services/Logging/Worker/log_worker_internal.h"
#include "SirinEmbedOS/Core/C/Fault.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <stdint.h>
#include <string.h>

const SRNOS_LogConfig SRNOS_LOG_DEFAULT_CONFIG = {
    .queue_depth = SRNOS_LOG_DEFAULT_QUEUE_DEPTH,
    .pool_count = SRNOS_LOG_DEFAULT_POOL_COUNT,
    .backend_mask = SRNOS_LOG_BACKEND_USB_CDC,
};

uint32_t SRNOS_LoggingHeapBudget(bool enabled)
{
    if (!enabled) return 0u;
    /* CMSIS memory-pool bookkeeping, TLS/alignment and allocator headers are
     * covered by the explicit margin; pool payload and queue/TCB are exact. */
    const size_t bytes = SRNOS_LOG_WORKER_STACK_BYTES + sizeof(StaticTask_t) +
        SRNOS_LOG_DEFAULT_QUEUE_DEPTH * sizeof(uintptr_t) + sizeof(StaticQueue_t) +
        SRNOS_LOG_DEFAULT_POOL_COUNT * sizeof(SRNOS_LogMsg) + SRNOS_LOG_RESOURCE_MARGIN_BYTES;
    return bytes > SRNOS_LOG_MINIMUM_HEAP_RESERVE_BYTES ? (uint32_t)bytes : SRNOS_LOG_MINIMUM_HEAP_RESERVE_BYTES;
}

static const osMessageQueueAttr_t s_log_queue_attr = {
    .name = "SRNOS_LogQueue",
};

static const osMemoryPoolAttr_t s_log_pool_attr = {
    .name = "SRNOS_LogPool",
};

static osMessageQueueId_t s_log_queue;
static osMemoryPoolId_t s_log_pool;
static uint32_t s_backend_mask;

static SRNOS_LogConfig normalize_config(const SRNOS_LogConfig* config)
{
    SRNOS_LogConfig out = SRNOS_LOG_DEFAULT_CONFIG;
    if (config != NULL) {
        out = *config;
        if (out.queue_depth == 0u) {
            out.queue_depth = SRNOS_LOG_DEFAULT_CONFIG.queue_depth;
        }
        if (out.pool_count == 0u) {
            out.pool_count = SRNOS_LOG_DEFAULT_CONFIG.pool_count;
        }
    }
    return out;
}

bool SRNOS_LogInitResources(const SRNOS_LogConfig* config)
{
    if (s_log_queue != NULL && s_log_pool != NULL) return true;
    const SRNOS_LogConfig cfg = normalize_config(config);
    s_backend_mask = cfg.backend_mask;

    if (s_log_queue == NULL) {
        s_log_queue = osMessageQueueNew(cfg.queue_depth,
                                        sizeof(uintptr_t),
                                        &s_log_queue_attr);
    }
    if (s_log_pool == NULL) {
        s_log_pool = osMemoryPoolNew(cfg.pool_count,
                                     sizeof(SRNOS_LogMsg),
                                     &s_log_pool_attr);
    }

    if (s_log_queue == NULL || s_log_pool == NULL) {
        return false;
    }

    SRNOS_LogInit();
    SRNOS_LogSetBackendMask(s_backend_mask);
    SRNOS_LogBindQueue(s_log_queue);
    SRNOS_LogBindPool(s_log_pool);
    return true;
}

typedef struct { bool received; uintptr_t message; char line[SRNOS_LOG_LINE_MAX]; } log_loop_t;
static bool log_wait(void *arg) {
 log_loop_t *c=arg;c->received=osMessageQueueGet(s_log_queue,&c->message,NULL,pdMS_TO_TICKS(10))==osOK;return true;
}
static void log_step(void *arg) {
 log_loop_t *c=arg;
 if(c->received) {
  SRNOS_LogMsg *msg=(SRNOS_LogMsg *)c->message;
  {
   bool truncated=false;size_t len=SRNOS_LogFormatLine(c->line,sizeof(c->line),msg,&truncated);
   if(truncated)SRNOS_LogRecordTruncatedBody();
   (void)SRNOS_LogOutputLineEx(c->line,len,msg->level,false);
  }
  osMemoryPoolFree(s_log_pool,msg);
 }
 SRNOS_LogServiceUsbTransport();
}
void SRNOS_LogTask(void* argument)
{
    const SRNOS_LogConfig* config = (const SRNOS_LogConfig*)argument;
    SRNOS_ASSERT(SRNOS_LogInitResources(config));

    const char* boot = "[ UNKNOWN  ][INFO][SRNOS_Log] Logging started.\r\n";
    (void)SRNOS_LogOutputLineEx(boot, strlen(boot), SRNOS_LOG_INFO, true);

    log_loop_t context={0};SRNOS_LogServiceUsbTransport();
    const SRNOS_InternalLoop loop={log_step,log_wait,&context,0,true};SRNOS_KernelRunLoop(&loop);

}
