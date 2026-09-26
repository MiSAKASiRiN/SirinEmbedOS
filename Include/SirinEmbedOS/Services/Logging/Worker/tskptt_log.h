#ifndef OMX_TSKPTT_LOG_H
#define OMX_TSKPTT_LOG_H

#include "cmsis_os2.h"
#include "SirinEmbedOS/Services/Logging/bsp_srn_log.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t queue_depth;
    uint32_t pool_count;
    uint32_t backend_mask;
} tskptt_log_config_t;

extern const tskptt_log_config_t TSKPTT_LOG_DEFAULT_CONFIG;

bool TSKPTT_Log_InitResources(const tskptt_log_config_t* config);
void TSKPTT_Log_Task(void* argument);
osMessageQueueId_t TSKPTT_Log_GetQueue(void);
osMemoryPoolId_t TSKPTT_Log_GetPool(void);

#ifdef __cplusplus
}
#endif

#endif /* OMX_TSKPTT_LOG_H */
