#pragma once
#include <stdint.h>
enum {
    SRNOS_CYCLE_ACTIVE=1u, SRNOS_CYCLE_PERIODIC=2u, SRNOS_CYCLE_MEASURED=4u,
    SRNOS_CYCLE_JITTER_VALID=8u, SRNOS_CYCLE_STALE=16u, SRNOS_CYCLE_SATURATED=32u
};
/* Observed wall time, not CPU instructions. Period jitter is actual start
 * interval minus period; event/relative loops have no period/jitter validity.
 * Counts/extrema cover the actual window, not the telemetry rotation period. */
typedef struct {
    uint64_t sampled_us, window_us;
    uint32_t generation,flags,period_ms,completed,jitter_samples;
    uint32_t execution_min_us,execution_mean_us,execution_max_us;
    uint32_t start_late_mean_us,start_late_max_us;
    int32_t jitter_min_us,jitter_max_us;
    uint32_t jitter_abs_mean_us,deadline_misses,skipped;
    uint32_t hook_mean_ns,hook_max_ns;
    uint8_t slot;
} SRNOS_MonitorCycle;
