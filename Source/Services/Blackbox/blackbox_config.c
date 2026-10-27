#include "blackbox_internal.h"
#include <limits.h>

void SRNOS_BlackboxDefaultConfig(SRNOS_BlackboxConfig *config)
{
    if (config) *config=(SRNOS_BlackboxConfig){.enabled=false,.queue_depth=SRNOS_BLACKBOX_QUEUE_DEFAULT};
}
bool SRNOS_BlackboxConfigValid(const SRNOS_BlackboxConfig *config)
{
    return config && ((!config->enabled && !config->queue_depth) ||
        (config->queue_depth>=8u && config->queue_depth<=64u));
}
uint32_t SRNOS_BlackboxHeapBudget(const SRNOS_BlackboxConfig *config)
{
    if (!SRNOS_BlackboxConfigValid(config)) return UINT32_MAX;
    if (!config->enabled) return 0;
    /* Service state is bounded by a compile-time assertion in blackbox.c.
     * No per-record allocations, no application-object pool consumption. */
    return SRNOS_BLACKBOX_STACK_BYTES + SRNOS_BLACKBOX_STATE_BUDGET +
        SRNOS_BLACKBOX_RTOS_RESERVE + config->queue_depth*sizeof(SRNOS_BlackboxRecord);
}
