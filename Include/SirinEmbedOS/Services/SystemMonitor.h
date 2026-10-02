#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "SirinEmbedOS/Diagnostics/Runtime/TaskStatistics.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SRNOS_MONITOR_MAX_TASKS 64u
#define SRNOS_MONITOR_PERIOD_US 1000000u
#define SRNOS_MONITOR_STALE_US 3000000u
/* ADC rank order: current, input voltage, MCU temperature, VREFINT, VBAT. */
enum {
    SRNOS_MONITOR_CURRENT_VALID=1u, SRNOS_MONITOR_VOLTAGE_VALID=2u,
    SRNOS_MONITOR_VBAT_VALID=4u, SRNOS_MONITOR_VREF_VALID=8u,
    SRNOS_MONITOR_TEMPERATURE_VALID=16u, SRNOS_MONITOR_VDDA_VALID=32u,
    SRNOS_MONITOR_CURRENT_LOW=1u, SRNOS_MONITOR_SATURATED=2u,
    SRNOS_MONITOR_CAL_INVALID=4u, SRNOS_MONITOR_ADC_STALE=8u
};
typedef enum {
    SRNOS_MONITOR_ADC_DISABLED, SRNOS_MONITOR_ADC_STARTING,
    SRNOS_MONITOR_ADC_READY, SRNOS_MONITOR_ADC_ERROR, SRNOS_MONITOR_ADC_QUARANTINED
} SRNOS_MonitorADCState;
typedef struct {
    uint64_t start_us, completed_us;
    uint32_t sequence, valid_fields, quality;
    uint16_t raw[5];
    float current_a, voltage_v, vbat_v, vrefint_v, temperature_c, vdda_v;
    SRNOS_MonitorADCState state;
    uint32_t errors, timeouts, recoveries;
} SRNOS_MonitorADC;
enum {
    SRNOS_MONITOR_TASK_ACTIVE=1u, SRNOS_MONITOR_TASK_CPU_VALID=2u,
    SRNOS_MONITOR_TASK_IDLE=4u, SRNOS_MONITOR_TASK_SELF=8u,
    SRNOS_MONITOR_TASK_STALE=16u,
    SRNOS_MONITOR_ENABLED=1u, SRNOS_MONITOR_CPU_VALID=2u,
    SRNOS_MONITOR_STALE=4u
};
typedef struct {
    uint64_t sampled_us, window_us, runtime_us;
    uint32_t generation, flags, cpu_ppm, switch_count, ready_count;
    uint32_t max_run_us, max_ready_us;
    uint16_t stack_free_words; /* UINT16_MAX: not measured; no periodic scan. */
    uint8_t slot, state, priority, base_priority;
    char name[16];
    SRNOS_MonitorCycle cycle;
} SRNOS_MonitorTask;
typedef struct {
    uint64_t updated_us;
    uint32_t sequence, flags;
    SRNOS_MonitorADC adc;
    uint32_t busy_ppm, idle_ppm, monitor_cpu_ppm, task_count;
    uint32_t max_critical_us, max_wake_late_us, skipped_cycles, slot_overflows;
} SRNOS_SystemMonitorSnapshot;
/* Task context only; bounded copies, no I/O or allocation. Returned booleans
 * validate arguments/context, not sensor health. Check flags/state/age.
 * Slots 1..64; inactive slots return a record without ACTIVE. CPU is ppm.
 * Runtime includes ISR time attributed by FreeRTOS, not pure task CPU time.
 * max_run is a scheduling slice; max_ready is Ready-to-running latency,
 * neither is an application loop duration/period jitter measurement.
 * Managed loop measurements are in cycle; inspect its independent flags. */
bool SRNOS_SystemMonitorGetSnapshot(SRNOS_SystemMonitorSnapshot *out);
bool SRNOS_SystemMonitorGetTask(uint8_t slot, SRNOS_MonitorTask *out);
#ifdef __cplusplus
}
#endif
