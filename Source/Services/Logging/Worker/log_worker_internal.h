#ifndef SRNOS_LOG_WORKER_INTERNAL_H
#define SRNOS_LOG_WORKER_INTERNAL_H

#include "cmsis_os2.h"
#include "Services/Logging/log_internal.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t queue_depth;
    uint32_t pool_count;
    uint32_t backend_mask;
} SRNOS_LogConfig;

extern const SRNOS_LogConfig SRNOS_LOG_DEFAULT_CONFIG;

bool SRNOS_LogInitResources(const SRNOS_LogConfig* config);
void SRNOS_LogTask(void* argument);

#ifdef __cplusplus
}
#endif

#endif /* SRNOS_LOG_WORKER_INTERNAL_H */
