#include "tskptt_hskp_internal.h"

#include "SirinEmbedOS/Diagnostics/Time/bsp_dwt.h"
#include "cmsis_os2.h"
#include "SirinEmbedOS/Diagnostics/Runtime/tskptt_hskp_wire.h"

#include <stdbool.h>
#include <string.h>

#define HSKP_TELEMETRY_PERIOD_MIN_MS (1000u)
#define HSKP_TELEMETRY_PERIOD_MAX_MS (60000u)

const tskptt_hskp_config_t TSKPTT_HSKP_DEFAULT_CONFIG = {
    .sample_period_ms = 1000u,
    .summary_period_ms = 5000u,
    .stack_scan_period_ms = 30000u,
    .warning_repeat_ms = 30000u,
    .late_warning_ms = 100u,
    .stack_warning_words = 128u,
    .stack_critical_words = 64u,
    .detail_rows_per_report = 4u,
    .max_warnings_per_report = 4u,
    .reserved = {0u, 0u},
    .publish = NULL,
    .publish_user = NULL,
};

typedef struct
{
    TaskHandle_t handle;
    char name[configMAX_TASK_NAME_LEN];
    uint64_t previous_runtime;
    uint64_t report_runtime;
    uint32_t detail_switch_count;
    uint32_t detail_ready_count;
    uint32_t window_switch_count;
    uint32_t window_ready_count;
    uint32_t detail_max_run_cycles;
    uint32_t detail_max_ready_cycles;
    uint32_t window_max_run_cycles;
    uint32_t window_max_ready_cycles;
    uint32_t last_stack_warning_tick;
    uint16_t stack_high_water_words;
    uint16_t cpu_permille;
    UBaseType_t current_priority;
    UBaseType_t base_priority;
    eTaskState state;
    uint8_t active;
    uint8_t seen;
    uint8_t baseline_valid;
    uint8_t stack_valid;
} hskp_task_record_t;

typedef struct
{
    uint64_t previous_total;
    uint64_t previous_idle;
    uint64_t report_total;
    uint64_t report_idle;
    uint32_t max_sample_cycles;
    uint32_t max_stack_scan_cycles;
    uint32_t max_late_ms;
    uint32_t publish_failures;
    uint32_t last_dwt_warning_tick;
    uint32_t last_runtime_warning_tick;
    uint32_t last_late_warning_tick;
    uint32_t last_slot_warning_tick;
    uint32_t last_publish_warning_tick;
    uint8_t total_baseline_valid;
    uint8_t idle_baseline_valid;
    uint8_t runtime_stuck;
    uint8_t dwt_failed;
} hskp_system_record_t;

typedef struct
{
    uint8_t enabled;
    uint32_t period_ms;
    uint32_t generation;
} hskp_telemetry_control_t;

static hskp_task_record_t s_task_records[TSKPTT_HSKP_MAX_TASKS + 1u];
static tskptt_hskp_trace_snapshot_t s_trace_snapshot[TSKPTT_HSKP_MAX_TASKS];
static hskp_system_record_t s_system;
static uint8_t s_wire_payload[TSKPTT_HSKP_WIRE_PAYLOAD_SIZE];
static UBaseType_t s_detail_cursor = 1u;
static uint32_t s_report_sequence;
static volatile uint8_t s_telemetry_enabled;
static volatile uint32_t s_telemetry_period_ms;
static volatile uint32_t s_telemetry_generation;

static void hskp_put_u16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8u);
}

static void hskp_put_u32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8u);
    out[2] = (uint8_t)(value >> 16u);
    out[3] = (uint8_t)(value >> 24u);
}

static uint8_t hskp_clamp_u8(UBaseType_t value)
{
    return (value > UINT8_MAX) ? UINT8_MAX : (uint8_t)value;
}

static uint8_t hskp_wire_state(eTaskState state)
{
    switch (state)
    {
    case eRunning:   return TSKPTT_HSKP_WIRE_STATE_RUNNING;
    case eReady:     return TSKPTT_HSKP_WIRE_STATE_READY;
    case eBlocked:   return TSKPTT_HSKP_WIRE_STATE_BLOCKED;
    case eSuspended: return TSKPTT_HSKP_WIRE_STATE_SUSPENDED;
    case eDeleted:   return TSKPTT_HSKP_WIRE_STATE_DELETED;
    default:         return TSKPTT_HSKP_WIRE_STATE_INVALID;
    }
}

static uint32_t hskp_cycles_to_us(uint32_t cycles)
{
    const uint32_t cpu_hz = DWT_GetCpuHz();
    if (cpu_hz == 0u)
    {
        return 0u;
    }
    return (uint32_t)(((uint64_t)cycles * 1000000ULL) / (uint64_t)cpu_hz);
}

static uint32_t hskp_cycles64_to_ms(uint64_t cycles)
{
    const uint32_t cpu_hz = DWT_GetCpuHz();
    if (cpu_hz == 0u)
    {
        return 0u;
    }
    return (uint32_t)((cycles * 1000ULL) / (uint64_t)cpu_hz);
}

static uint32_t hskp_ticks_to_ms(TickType_t ticks)
{
    return (uint32_t)(((uint64_t)ticks * 1000ULL) /
                      (uint64_t)configTICK_RATE_HZ);
}

static uint8_t hskp_tick_deadline_reached(TickType_t now,
                                          TickType_t deadline)
{
    const TickType_t half_range = (TickType_t)(~(TickType_t)0u >> 1u);
    return (uint8_t)((TickType_t)(now - deadline) <= half_range);
}

static TickType_t hskp_advance_deadline(TickType_t now,
                                        TickType_t deadline,
                                        TickType_t period)
{
    do
    {
        deadline += period;
    }
    while (hskp_tick_deadline_reached(now, deadline) != 0u);
    return deadline;
}

static uint16_t hskp_permille(uint64_t part, uint64_t total)
{
    if (total == 0u)
    {
        return 0u;
    }
    uint64_t value = (part * 1000ULL + total / 2ULL) / total;
    if (value > 1000ULL)
    {
        value = 1000ULL;
    }
    return (uint16_t)value;
}

static uint8_t hskp_warning_due(uint32_t now_tick,
                                uint32_t *last_tick,
                                uint32_t repeat_ms)
{
    const uint32_t repeat_ticks = (uint32_t)pdMS_TO_TICKS(repeat_ms);
    if ((*last_tick == 0u) ||
        ((uint32_t)(now_tick - *last_tick) >= repeat_ticks))
    {
        *last_tick = now_tick;
        return 1u;
    }
    return 0u;
}

void TSKPTT_HSKP_TelemetryConfigure(bool enabled, uint32_t period_us)
{
    uint32_t period_ms = period_us / 1000u;
    if ((period_us % 1000u) != 0u)
    {
        period_ms++;
    }
    if (period_ms == 0u)
    {
        period_ms = TSKPTT_HSKP_DEFAULT_CONFIG.summary_period_ms;
    }
    if (period_ms < HSKP_TELEMETRY_PERIOD_MIN_MS)
    {
        period_ms = HSKP_TELEMETRY_PERIOD_MIN_MS;
    }
    if (period_ms > HSKP_TELEMETRY_PERIOD_MAX_MS)
    {
        period_ms = HSKP_TELEMETRY_PERIOD_MAX_MS;
    }

    taskENTER_CRITICAL();
    s_telemetry_enabled = enabled ? 1u : 0u;
    s_telemetry_period_ms = period_ms;
    s_telemetry_generation++;
    taskEXIT_CRITICAL();
}

static hskp_telemetry_control_t hskp_get_telemetry_control(void)
{
    hskp_telemetry_control_t control;
    taskENTER_CRITICAL();
    control.enabled = s_telemetry_enabled;
    control.period_ms = s_telemetry_period_ms;
    control.generation = s_telemetry_generation;
    taskEXIT_CRITICAL();
    return control;
}

static void hskp_reset_record(hskp_task_record_t *record,
                              const tskptt_hskp_trace_snapshot_t *trace)
{
    memset(record, 0, sizeof(*record));
    record->handle = trace->handle;
    memcpy(record->name, trace->name, sizeof(record->name));
    record->active = 1u;
    record->seen = 1u;
}

static void hskp_accumulate_trace(hskp_task_record_t *record,
                                  const tskptt_hskp_trace_snapshot_t *trace)
{
    record->window_switch_count += trace->switch_count;
    record->window_ready_count += trace->ready_count;
    record->detail_switch_count += trace->switch_count;
    record->detail_ready_count += trace->ready_count;

    if (trace->max_run_cycles > record->window_max_run_cycles)
    {
        record->window_max_run_cycles = trace->max_run_cycles;
    }
    if (trace->max_run_cycles > record->detail_max_run_cycles)
    {
        record->detail_max_run_cycles = trace->max_run_cycles;
    }
    if (trace->max_ready_cycles > record->window_max_ready_cycles)
    {
        record->window_max_ready_cycles = trace->max_ready_cycles;
    }
    if (trace->max_ready_cycles > record->detail_max_ready_cycles)
    {
        record->detail_max_ready_cycles = trace->max_ready_cycles;
    }
}

static void hskp_sample(uint8_t scan_stacks)
{
    for (UBaseType_t i = 1u; i <= TSKPTT_HSKP_MAX_TASKS; ++i)
    {
        s_task_records[i].seen = 0u;
    }

    const uint32_t pause_start = DWT_GetCounter();
    vTaskSuspendAll();
    const size_t trace_count = tskptt_hskp_trace_snapshot(
        s_trace_snapshot, TSKPTT_HSKP_MAX_TASKS, 1u);
    for (size_t i = 0u; i < trace_count; ++i)
    {
        const tskptt_hskp_trace_snapshot_t *trace = &s_trace_snapshot[i];
        if ((trace->task_number == 0u) ||
            (trace->task_number > TSKPTT_HSKP_MAX_TASKS) ||
            (trace->handle == NULL))
        {
            continue;
        }

        hskp_task_record_t *record = &s_task_records[trace->task_number];
        if ((record->active == 0u) || (record->handle != trace->handle))
        {
            hskp_reset_record(record, trace);
        }
        else
        {
            record->seen = 1u;
        }

        TaskStatus_t status;
        memset(&status, 0, sizeof(status));
        vTaskGetInfo(trace->handle, &status, pdFALSE, eInvalid);
        record->state = status.eCurrentState;
        record->current_priority = status.uxCurrentPriority;
        record->base_priority = status.uxBasePriority;

        const uint64_t runtime = status.ulRunTimeCounter;
        if (record->baseline_valid != 0u)
        {
            record->report_runtime += runtime - record->previous_runtime;
        }
        record->previous_runtime = runtime;
        record->baseline_valid = 1u;

        if (scan_stacks != 0u)
        {
            UBaseType_t words = uxTaskGetStackHighWaterMark(trace->handle);
            if (words > UINT16_MAX)
            {
                words = UINT16_MAX;
            }
            record->stack_high_water_words = (uint16_t)words;
            record->stack_valid = 1u;
        }

        hskp_accumulate_trace(record, trace);
    }

    const uint64_t idle_runtime = ulTaskGetIdleRunTimeCounter();
    (void)xTaskResumeAll();
    const uint32_t pause_cycles = DWT_GetElapsedCycles(pause_start,
                                                       DWT_GetCounter());

    if (scan_stacks != 0u)
    {
        if (pause_cycles > s_system.max_stack_scan_cycles)
        {
            s_system.max_stack_scan_cycles = pause_cycles;
        }
    }
    else if (pause_cycles > s_system.max_sample_cycles)
    {
        s_system.max_sample_cycles = pause_cycles;
    }

    for (UBaseType_t i = 1u; i <= TSKPTT_HSKP_MAX_TASKS; ++i)
    {
        if ((s_task_records[i].active != 0u) &&
            (s_task_records[i].seen == 0u))
        {
            memset(&s_task_records[i], 0, sizeof(s_task_records[i]));
        }
    }

    const uint64_t total_runtime = TSKPTT_HSKP_RuntimeCounterGet();
    if (s_system.total_baseline_valid != 0u)
    {
        const uint64_t delta = total_runtime - s_system.previous_total;
        s_system.report_total += delta;
        if (delta == 0u)
        {
            s_system.runtime_stuck = 1u;
        }
    }
    s_system.previous_total = total_runtime;
    s_system.total_baseline_valid = 1u;

    if (s_system.idle_baseline_valid != 0u)
    {
        s_system.report_idle += idle_runtime - s_system.previous_idle;
    }
    s_system.previous_idle = idle_runtime;
    s_system.idle_baseline_valid = 1u;

    if (!DWT_IsReady())
    {
        s_system.dwt_failed = 1u;
    }
}

static UBaseType_t hskp_active_task_count(void)
{
    UBaseType_t count = 0u;
    for (UBaseType_t i = 1u; i <= TSKPTT_HSKP_MAX_TASKS; ++i)
    {
        if (s_task_records[i].active != 0u)
        {
            count++;
        }
    }
    return count;
}

static void hskp_wire_add_warning(uint8_t *warning_count,
                                  uint8_t code,
                                  uint8_t severity,
                                  uint8_t slot,
                                  uint32_t value)
{
    const size_t offset = TSKPTT_HSKP_WIRE_WARNINGS_OFFSET +
                          (size_t)(*warning_count) *
                              TSKPTT_HSKP_WIRE_WARNING_SIZE;
    s_wire_payload[offset + 0u] = code;
    s_wire_payload[offset + 1u] = severity;
    s_wire_payload[offset + 2u] = slot;
    hskp_put_u32(&s_wire_payload[offset + 4u], value);
    (*warning_count)++;
}

static uint8_t hskp_wire_warnings(const tskptt_hskp_config_t *config,
                                  uint32_t now_tick,
                                  uint8_t *flags)
{
    uint8_t warning_count = 0u;
    const uint8_t limit = config->max_warnings_per_report;
    const uint32_t slot_overflow = tskptt_hskp_trace_get_overflow_count();

    if (DWT_IsReady())
    {
        *flags |= TSKPTT_HSKP_WIRE_FLAG_DWT_READY;
    }
    if (s_system.dwt_failed != 0u)
    {
        if ((warning_count < limit) &&
            hskp_warning_due(now_tick, &s_system.last_dwt_warning_tick,
                             config->warning_repeat_ms))
        {
            hskp_wire_add_warning(&warning_count,
                                  TSKPTT_HSKP_WIRE_WARNING_DWT_UNAVAILABLE,
                                  TSKPTT_HSKP_WIRE_SEVERITY_ERROR, 0u, 0u);
        }
    }
    if (s_system.runtime_stuck != 0u)
    {
        *flags |= TSKPTT_HSKP_WIRE_FLAG_RUNTIME_STUCK;
        if ((warning_count < limit) &&
            hskp_warning_due(now_tick, &s_system.last_runtime_warning_tick,
                             config->warning_repeat_ms))
        {
            hskp_wire_add_warning(&warning_count,
                                  TSKPTT_HSKP_WIRE_WARNING_RUNTIME_STALLED,
                                  TSKPTT_HSKP_WIRE_SEVERITY_ERROR, 0u, 0u);
        }
    }
    if (slot_overflow != 0u)
    {
        *flags |= TSKPTT_HSKP_WIRE_FLAG_SLOT_OVERFLOW;
        if ((warning_count < limit) &&
            hskp_warning_due(now_tick, &s_system.last_slot_warning_tick,
                             config->warning_repeat_ms))
        {
            hskp_wire_add_warning(&warning_count,
                                  TSKPTT_HSKP_WIRE_WARNING_SLOT_OVERFLOW,
                                  TSKPTT_HSKP_WIRE_SEVERITY_WARNING, 0u,
                                  slot_overflow);
        }
    }
    if (s_system.max_late_ms > config->late_warning_ms)
    {
        *flags |= TSKPTT_HSKP_WIRE_FLAG_WAKE_LATE;
        if ((warning_count < limit) &&
            hskp_warning_due(now_tick, &s_system.last_late_warning_tick,
                             config->warning_repeat_ms))
        {
            hskp_wire_add_warning(&warning_count,
                                  TSKPTT_HSKP_WIRE_WARNING_WAKE_LATE,
                                  TSKPTT_HSKP_WIRE_SEVERITY_WARNING, 0u,
                                  s_system.max_late_ms);
        }
    }
    if (s_system.publish_failures != 0u)
    {
        *flags |= TSKPTT_HSKP_WIRE_FLAG_PUBLISH_FAILURE;
        if ((warning_count < limit) &&
            hskp_warning_due(now_tick, &s_system.last_publish_warning_tick,
                             config->warning_repeat_ms))
        {
            hskp_wire_add_warning(&warning_count,
                                  TSKPTT_HSKP_WIRE_WARNING_PUBLISH_FAILURE,
                                  TSKPTT_HSKP_WIRE_SEVERITY_WARNING, 0u,
                                  s_system.publish_failures);
        }
    }

    for (UBaseType_t i = 1u; i <= TSKPTT_HSKP_MAX_TASKS; ++i)
    {
        hskp_task_record_t *record = &s_task_records[i];
        if ((record->active == 0u) || (record->stack_valid == 0u) ||
            (record->stack_high_water_words >= config->stack_warning_words))
        {
            continue;
        }
        *flags |= TSKPTT_HSKP_WIRE_FLAG_STACK_WARNING;
        if ((warning_count >= limit) ||
            !hskp_warning_due(now_tick, &record->last_stack_warning_tick,
                              config->warning_repeat_ms))
        {
            continue;
        }
        const uint8_t severity =
            (record->stack_high_water_words < config->stack_critical_words)
                ? TSKPTT_HSKP_WIRE_SEVERITY_ERROR
                : TSKPTT_HSKP_WIRE_SEVERITY_WARNING;
        hskp_wire_add_warning(&warning_count,
                              TSKPTT_HSKP_WIRE_WARNING_STACK_LOW,
                              severity, (uint8_t)i,
                              record->stack_high_water_words);
    }
    return warning_count;
}

static uint8_t hskp_wire_task_details(const tskptt_hskp_config_t *config)
{
    uint8_t detail_count = 0u;
    UBaseType_t visited = 0u;
    while ((detail_count < config->detail_rows_per_report) &&
           (visited < TSKPTT_HSKP_MAX_TASKS))
    {
        if ((s_detail_cursor == 0u) ||
            (s_detail_cursor > TSKPTT_HSKP_MAX_TASKS))
        {
            s_detail_cursor = 1u;
        }

        const UBaseType_t task_number = s_detail_cursor++;
        visited++;
        hskp_task_record_t *record = &s_task_records[task_number];
        if (record->active == 0u)
        {
            continue;
        }

        const size_t offset = TSKPTT_HSKP_WIRE_TASKS_OFFSET +
                              (size_t)detail_count *
                                  TSKPTT_HSKP_WIRE_TASK_SIZE;
        s_wire_payload[offset + 0u] = (uint8_t)task_number;
        s_wire_payload[offset + 1u] = hskp_wire_state(record->state);
        s_wire_payload[offset + 2u] = hskp_clamp_u8(record->current_priority);
        s_wire_payload[offset + 3u] = hskp_clamp_u8(record->base_priority);
        hskp_put_u16(&s_wire_payload[offset + 4u], record->cpu_permille);
        hskp_put_u16(&s_wire_payload[offset + 6u],
                     (record->stack_valid != 0u)
                         ? record->stack_high_water_words
                         : 0u);
        hskp_put_u32(&s_wire_payload[offset + 8u],
                     hskp_cycles_to_us(record->detail_max_run_cycles));
        hskp_put_u32(&s_wire_payload[offset + 12u],
                     hskp_cycles_to_us(record->detail_max_ready_cycles));
        hskp_put_u32(&s_wire_payload[offset + 16u],
                     record->detail_switch_count);
        for (size_t n = 0u;
             (n < 15u) && (n < sizeof(record->name)) &&
             (record->name[n] != '\0');
             ++n)
        {
            s_wire_payload[offset + 20u + n] = (uint8_t)record->name[n];
        }

        record->detail_switch_count = 0u;
        record->detail_ready_count = 0u;
        record->detail_max_run_cycles = 0u;
        record->detail_max_ready_cycles = 0u;
        detail_count++;
    }
    return detail_count;
}

static void hskp_reset_report_window(void)
{
    for (UBaseType_t i = 1u; i <= TSKPTT_HSKP_MAX_TASKS; ++i)
    {
        hskp_task_record_t *record = &s_task_records[i];
        record->report_runtime = 0u;
        record->window_switch_count = 0u;
        record->window_ready_count = 0u;
        record->window_max_run_cycles = 0u;
        record->window_max_ready_cycles = 0u;
    }
    s_system.report_total = 0u;
    s_system.report_idle = 0u;
    s_system.max_sample_cycles = 0u;
    s_system.max_late_ms = 0u;
    s_system.runtime_stuck = 0u;
    s_system.dwt_failed = 0u;
}

static void hskp_reset_detail_windows(void)
{
    for (UBaseType_t i = 1u; i <= TSKPTT_HSKP_MAX_TASKS; ++i)
    {
        hskp_task_record_t *record = &s_task_records[i];
        record->detail_switch_count = 0u;
        record->detail_ready_count = 0u;
        record->detail_max_run_cycles = 0u;
        record->detail_max_ready_cycles = 0u;
    }
}

static void hskp_publish_report(const tskptt_hskp_config_t *config,
                                uint32_t now_tick,
                                uint8_t telemetry_enabled)
{
    if ((telemetry_enabled == 0u) || (config->publish == NULL))
    {
        hskp_reset_detail_windows();
        hskp_reset_report_window();
        return;
    }

    const uint16_t idle_permille = hskp_permille(s_system.report_idle,
                                                  s_system.report_total);
    const uint16_t busy_permille = (idle_permille <= 1000u)
                                       ? (uint16_t)(1000u - idle_permille)
                                       : 0u;
    UBaseType_t top_task = 0u;
    uint16_t top_cpu = 0u;
    uint32_t max_run_cycles = 0u;
    uint32_t max_ready_cycles = 0u;

    for (UBaseType_t i = 1u; i <= TSKPTT_HSKP_MAX_TASKS; ++i)
    {
        hskp_task_record_t *record = &s_task_records[i];
        if (record->active == 0u)
        {
            continue;
        }
        record->cpu_permille = hskp_permille(record->report_runtime,
                                             s_system.report_total);
        if (record->cpu_permille > top_cpu)
        {
            top_cpu = record->cpu_permille;
            top_task = i;
        }
        if (record->window_max_run_cycles > max_run_cycles)
        {
            max_run_cycles = record->window_max_run_cycles;
        }
        if (record->window_max_ready_cycles > max_ready_cycles)
        {
            max_ready_cycles = record->window_max_ready_cycles;
        }
    }

    memset(s_wire_payload, 0, sizeof(s_wire_payload));
    uint8_t flags = 0u;
    const uint8_t detail_count = hskp_wire_task_details(config);
    const uint8_t warning_count = hskp_wire_warnings(config, now_tick, &flags);

    s_wire_payload[0] = TSKPTT_HSKP_WIRE_VERSION;
    s_wire_payload[1] = flags;
    s_wire_payload[2] = detail_count;
    s_wire_payload[3] = warning_count;
    hskp_put_u32(&s_wire_payload[4],
                 hskp_cycles64_to_ms(s_system.report_total));
    hskp_put_u16(&s_wire_payload[8], busy_permille);
    hskp_put_u16(&s_wire_payload[10], idle_permille);
    s_wire_payload[12] = hskp_clamp_u8(hskp_active_task_count());
    s_wire_payload[13] = hskp_clamp_u8(top_task);
    hskp_put_u16(&s_wire_payload[14], top_cpu);
    hskp_put_u32(&s_wire_payload[16], hskp_cycles_to_us(max_run_cycles));
    hskp_put_u32(&s_wire_payload[20], hskp_cycles_to_us(max_ready_cycles));
    hskp_put_u32(&s_wire_payload[24],
                 hskp_cycles_to_us(s_system.max_sample_cycles));
    hskp_put_u32(&s_wire_payload[28],
                 hskp_cycles_to_us(s_system.max_stack_scan_cycles));
    hskp_put_u32(&s_wire_payload[32], s_system.max_late_ms);
    hskp_put_u32(&s_wire_payload[36],
                 tskptt_hskp_trace_get_overflow_count());
    hskp_put_u32(&s_wire_payload[40], s_system.publish_failures);

    const uint32_t sequence = s_report_sequence++;
    const int32_t result = config->publish(
        config->publish_user, sequence, (uint32_t)DWT_GetUptimeUs(),
        s_wire_payload, sizeof(s_wire_payload));
    if (result != 0)
    {
        s_system.publish_failures++;
    }

    hskp_reset_report_window();
}

static void hskp_sanitize_config(tskptt_hskp_config_t *config)
{
    if (config->sample_period_ms == 0u)
    {
        config->sample_period_ms = TSKPTT_HSKP_DEFAULT_CONFIG.sample_period_ms;
    }
    if (config->summary_period_ms < config->sample_period_ms)
    {
        config->summary_period_ms = config->sample_period_ms;
    }
    if (config->stack_scan_period_ms < config->summary_period_ms)
    {
        config->stack_scan_period_ms = config->summary_period_ms;
    }
    if (config->warning_repeat_ms < config->summary_period_ms)
    {
        config->warning_repeat_ms = config->summary_period_ms;
    }
    if (config->detail_rows_per_report == 0u)
    {
        config->detail_rows_per_report = 1u;
    }
    if (config->detail_rows_per_report > TSKPTT_HSKP_WIRE_MAX_TASK_DETAILS)
    {
        config->detail_rows_per_report = TSKPTT_HSKP_WIRE_MAX_TASK_DETAILS;
    }
    if (config->max_warnings_per_report == 0u)
    {
        config->max_warnings_per_report = 1u;
    }
    if (config->max_warnings_per_report > TSKPTT_HSKP_WIRE_MAX_WARNINGS)
    {
        config->max_warnings_per_report = TSKPTT_HSKP_WIRE_MAX_WARNINGS;
    }
}

void TSKPTT_HSKP_Task(void *argument)
{
    tskptt_hskp_config_t config = TSKPTT_HSKP_DEFAULT_CONFIG;
    if (argument != NULL)
    {
        config = *(const tskptt_hskp_config_t *)argument;
    }
    hskp_sanitize_config(&config);

    DWT_Init(0u);
    TickType_t sample_ticks = pdMS_TO_TICKS(config.sample_period_ms);
    TickType_t summary_ticks = pdMS_TO_TICKS(config.summary_period_ms);
    TickType_t stack_ticks = pdMS_TO_TICKS(config.stack_scan_period_ms);
    if (sample_ticks == 0u)
    {
        sample_ticks = 1u;
    }
    if (summary_ticks == 0u)
    {
        summary_ticks = sample_ticks;
    }
    if (stack_ticks == 0u)
    {
        stack_ticks = summary_ticks;
    }

    TickType_t last_wake = xTaskGetTickCount();
    TickType_t next_summary = last_wake + summary_ticks;
    TickType_t next_stack_scan = last_wake + stack_ticks;
    uint32_t telemetry_generation = UINT32_MAX;
    uint8_t telemetry_enabled = 0u;

    for (;;)
    {
        vTaskDelayUntil(&last_wake, sample_ticks);
        const TickType_t now = xTaskGetTickCount();
        const uint32_t late_ms = hskp_ticks_to_ms(now - last_wake);
        if (late_ms > s_system.max_late_ms)
        {
            s_system.max_late_ms = late_ms;
        }

        const uint8_t scan_stacks = hskp_tick_deadline_reached(
            now, next_stack_scan);
        hskp_sample(scan_stacks);
        if (scan_stacks != 0u)
        {
            next_stack_scan = hskp_advance_deadline(now, next_stack_scan,
                                                    stack_ticks);
        }

        const hskp_telemetry_control_t control =
            hskp_get_telemetry_control();
        if (control.generation != telemetry_generation)
        {
            telemetry_generation = control.generation;
            telemetry_enabled = control.enabled;
            const uint32_t period_ms = (control.period_ms != 0u)
                                           ? control.period_ms
                                           : config.summary_period_ms;
            summary_ticks = pdMS_TO_TICKS(period_ms);
            if (summary_ticks < sample_ticks)
            {
                summary_ticks = sample_ticks;
            }
            next_summary = now + summary_ticks;
            hskp_reset_detail_windows();
            hskp_reset_report_window();
        }

        if (hskp_tick_deadline_reached(now, next_summary) != 0u)
        {
            hskp_publish_report(&config, (uint32_t)now,
                                telemetry_enabled);
            next_summary = hskp_advance_deadline(now, next_summary,
                                                 summary_ticks);
        }
    }
}
