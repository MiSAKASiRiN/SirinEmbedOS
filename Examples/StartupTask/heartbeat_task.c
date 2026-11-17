#include "SirinEmbedOS/Kernel/System.h"

/* The task is the only writer. Watch this counter in the debugger. */
static volatile uint32_t heartbeat_count;

static SRNOS_StepResult heartbeat_step(void *argument, const SRNOS_ThreadWake *wake)
{
    (void)argument;
    (void)wake;
    ++heartbeat_count;
    return SRNOS_STEP_CONTINUE;
}

bool AppRegisterHeartbeatTask(SRNOS_SystemConfig *config)
{
    if (config == NULL || config->thread_count >= SRNOS_SYSTEM_MAX_THREADS) {
        return false;
    }
    const SRNOS_ThreadConfig task = {
        .name = "Heartbeat",
        .step = heartbeat_step,
        .stack_bytes = 4096,
        .priority = 24,
        .mode = SRNOS_TASK_PERIODIC,
        .period_ms = 1000,
    };
    config->threads[config->thread_count++] = task;
    return true;
}
