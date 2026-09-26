#include "SirinEmbedOS/Services/Logging/Worker/tskptt_log.h"
#include "SirinEmbedOS/Core/C/Fault.h"

#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>
#include <string.h>

const tskptt_log_config_t TSKPTT_LOG_DEFAULT_CONFIG = {
    .queue_depth = 20u,
    .pool_count = 64u,
    .backend_mask = SRN_LOG_BACKEND_USB_CDC,
};

static const osMessageQueueAttr_t s_log_queue_attr = {
    .name = "Log_Queue",
};

static const osMemoryPoolAttr_t s_log_pool_attr = {
    .name = "Log_Pool",
};

static osMessageQueueId_t s_log_queue;
static osMemoryPoolId_t s_log_pool;
static uint32_t s_backend_mask;

static tskptt_log_config_t normalize_config(const tskptt_log_config_t* config)
{
    tskptt_log_config_t out = TSKPTT_LOG_DEFAULT_CONFIG;
    if (config != NULL) {
        out = *config;
        if (out.queue_depth == 0u) {
            out.queue_depth = TSKPTT_LOG_DEFAULT_CONFIG.queue_depth;
        }
        if (out.pool_count == 0u) {
            out.pool_count = TSKPTT_LOG_DEFAULT_CONFIG.pool_count;
        }
    }
    return out;
}

bool TSKPTT_Log_InitResources(const tskptt_log_config_t* config)
{
    if (s_log_queue != NULL && s_log_pool != NULL) return true;
    const tskptt_log_config_t cfg = normalize_config(config);
    s_backend_mask = cfg.backend_mask;

    if (s_log_queue == NULL) {
        s_log_queue = osMessageQueueNew(cfg.queue_depth,
                                        sizeof(uintptr_t),
                                        &s_log_queue_attr);
    }
    if (s_log_pool == NULL) {
        s_log_pool = osMemoryPoolNew(cfg.pool_count,
                                     sizeof(srn_log_msg_t),
                                     &s_log_pool_attr);
    }

    if (s_log_queue == NULL || s_log_pool == NULL) {
        return false;
    }

    SRN_Log_Init();
    SRN_Log_SetBackendMask(s_backend_mask);
    SRN_Log_BindQueue(s_log_queue);
    SRN_Log_BindPool(s_log_pool);
    return true;
}

osMessageQueueId_t TSKPTT_Log_GetQueue(void)
{
    return s_log_queue;
}

osMemoryPoolId_t TSKPTT_Log_GetPool(void)
{
    return s_log_pool;
}

void TSKPTT_Log_Task(void* argument)
{
    const tskptt_log_config_t* config = (const tskptt_log_config_t*)argument;
    SRNOS_ASSERT(TSKPTT_Log_InitResources(config));

    const char* boot = "[  UNKNOW  ][INFO][LOG_Task] Log_Task Started.\r\n";
    (void)SRN_LogOutputLineEx(boot, strlen(boot), SRN_LOG_INFO, true);

    char line[SRN_LOG_LINE_MAX];
    for (;;) {
        SRN_Log_ServiceUsbTransport();
        uintptr_t pv;
        if (osMessageQueueGet(s_log_queue, &pv, NULL, pdMS_TO_TICKS(10)) == osOK) {
            srn_log_msg_t* msg = (srn_log_msg_t*)pv;

            if (msg->level == SRN_LOG_DEBUG &&
                strncmp(msg->source, "EXFDCAN0_RX", SRN_LOG_SRC_MAX) == 0) {
                SRN_Log_RecordDroppedQueue();
                osMemoryPoolFree(s_log_pool, msg);
                continue;
            }

            bool truncated = false;
            size_t len = SRN_LogFormatLine(line, sizeof(line), msg, &truncated);
            if (truncated) {
                SRN_Log_RecordTruncatedBody();
            }
            (void)SRN_LogOutputLineEx(line, len, msg->level, false);

            osMemoryPoolFree(s_log_pool, msg);
        }
    }
}
