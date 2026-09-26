#include "tskptt_hskp_internal.h"

#include "SirinEmbedOS/Diagnostics/Time/bsp_dwt.h"
#include <string.h>

typedef struct
{
    TaskHandle_t handle;
    UBaseType_t task_number;
    char name[configMAX_TASK_NAME_LEN];
    volatile uint32_t ready_counter;
    volatile uint32_t switch_in_counter;
    volatile uint32_t switch_count;
    volatile uint32_t ready_count;
    volatile uint32_t max_run_cycles;
    volatile uint32_t max_ready_cycles;
    volatile uint8_t active;
    volatile uint8_t ready_valid;
    volatile uint8_t running;
    uint8_t reserved;
} hskp_trace_slot_t;

/* Index zero is intentionally unused so task numbers 1..64 map directly. */
static hskp_trace_slot_t s_trace_slots[TSKPTT_HSKP_MAX_TASKS + 1u];
static volatile uint32_t s_trace_slot_overflow;
static uint64_t s_runtime_counter_base;
static uint8_t s_runtime_counter_initialized;

static UBaseType_t hskp_trace_allocate_slot(void)
{
    for (UBaseType_t slot_number = 1u;
         slot_number <= TSKPTT_HSKP_MAX_TASKS;
         ++slot_number)
    {
        if (s_trace_slots[slot_number].active == 0u)
        {
            return slot_number;
        }
    }
    return 0u;
}

static hskp_trace_slot_t *hskp_trace_slot_from_handle(void *task_handle)
{
    if (task_handle == NULL)
    {
        return NULL;
    }

    UBaseType_t task_number = uxTaskGetTaskNumber((TaskHandle_t)task_handle);
    if ((task_number == 0u) || (task_number > TSKPTT_HSKP_MAX_TASKS))
    {
        return NULL;
    }
    return &s_trace_slots[task_number];
}

static void hskp_trace_update_max(volatile uint32_t *maximum, uint32_t value)
{
    if (value > *maximum)
    {
        *maximum = value;
    }
}

void TSKPTT_HSKP_RuntimeStatsInit(void)
{
    if (s_runtime_counter_initialized != 0u)
    {
        return;
    }

    DWT_Init(0u);
    s_runtime_counter_base = DWT_GetUptimeCycles64();
    s_runtime_counter_initialized = 1u;
}

uint64_t TSKPTT_HSKP_RuntimeCounterGet(void)
{
    if (s_runtime_counter_initialized == 0u)
    {
        TSKPTT_HSKP_RuntimeStatsInit();
    }
    return DWT_GetUptimeCycles64() - s_runtime_counter_base;
}

void TSKPTT_HSKP_TraceTaskCreate(void *task_handle)
{
    if (task_handle == NULL)
    {
        return;
    }

    TaskHandle_t handle = (TaskHandle_t)task_handle;
    UBaseType_t task_number = hskp_trace_allocate_slot();
    if (task_number == 0u)
    {
        vTaskSetTaskNumber(handle, 0u);
        s_trace_slot_overflow++;
        return;
    }

    hskp_trace_slot_t *slot = &s_trace_slots[task_number];
    slot->active = 0u;
    slot->handle = handle;
    slot->task_number = task_number;
    slot->ready_counter = 0u;
    slot->switch_in_counter = 0u;
    slot->switch_count = 0u;
    slot->ready_count = 0u;
    slot->max_run_cycles = 0u;
    slot->max_ready_cycles = 0u;
    slot->ready_valid = 0u;
    slot->running = 0u;
    slot->reserved = 0u;

    const char *name = pcTaskGetName(handle);
    if (name == NULL)
    {
        name = "?";
    }
    size_t i = 0u;
    for (; (i + 1u < sizeof(slot->name)) && (name[i] != '\0'); ++i)
    {
        slot->name[i] = name[i];
    }
    slot->name[i] = '\0';
    for (++i; i < sizeof(slot->name); ++i)
    {
        slot->name[i] = '\0';
    }
    vTaskSetTaskNumber(handle, task_number);
    slot->active = 1u;
}

void TSKPTT_HSKP_TraceTaskDelete(void *task_handle)
{
    hskp_trace_slot_t *slot = hskp_trace_slot_from_handle(task_handle);
    if ((slot == NULL) || (slot->handle != (TaskHandle_t)task_handle))
    {
        return;
    }

    slot->active = 0u;
    slot->ready_valid = 0u;
    slot->running = 0u;
    slot->handle = NULL;
    slot->task_number = 0u;
    vTaskSetTaskNumber((TaskHandle_t)task_handle, 0u);
}

void TSKPTT_HSKP_TraceTaskReady(void *task_handle)
{
    hskp_trace_slot_t *slot = hskp_trace_slot_from_handle(task_handle);
    if ((slot == NULL) || (slot->active == 0u) || !DWT_IsReady())
    {
        return;
    }

    slot->ready_counter = DWT_GetCounter();
    slot->ready_valid = 1u;
    slot->ready_count++;
}

void TSKPTT_HSKP_TraceTaskSwitchedIn(void)
{
    TaskHandle_t handle = xTaskGetCurrentTaskHandle();
    hskp_trace_slot_t *slot = hskp_trace_slot_from_handle(handle);
    if ((slot == NULL) || (slot->active == 0u) || !DWT_IsReady())
    {
        return;
    }

    uint32_t now = DWT_GetCounter();
    if (slot->ready_valid != 0u)
    {
        uint32_t latency = DWT_GetElapsedCycles(slot->ready_counter, now);
        hskp_trace_update_max(&slot->max_ready_cycles, latency);
        slot->ready_valid = 0u;
    }
    slot->switch_in_counter = now;
    slot->running = 1u;
    slot->switch_count++;
}

void TSKPTT_HSKP_TraceTaskSwitchedOut(void)
{
    TaskHandle_t handle = xTaskGetCurrentTaskHandle();
    hskp_trace_slot_t *slot = hskp_trace_slot_from_handle(handle);
    if ((slot == NULL) || (slot->active == 0u) ||
        (slot->running == 0u) || !DWT_IsReady())
    {
        return;
    }

    uint32_t now = DWT_GetCounter();
    uint32_t run_cycles = DWT_GetElapsedCycles(slot->switch_in_counter, now);
    hskp_trace_update_max(&slot->max_run_cycles, run_cycles);
    slot->running = 0u;
}

size_t tskptt_hskp_trace_snapshot(tskptt_hskp_trace_snapshot_t *out,
                                  size_t capacity,
                                  uint8_t reset_window)
{
    if ((out == NULL) || (capacity == 0u))
    {
        return 0u;
    }

    size_t count = 0u;
    taskENTER_CRITICAL();
    for (UBaseType_t task_number = 1u;
         (task_number <= TSKPTT_HSKP_MAX_TASKS) && (count < capacity);
         ++task_number)
    {
        hskp_trace_slot_t *slot = &s_trace_slots[task_number];
        if ((slot->active == 0u) || (slot->handle == NULL))
        {
            continue;
        }

        out[count].handle = slot->handle;
        out[count].task_number = slot->task_number;
        memcpy(out[count].name, slot->name, sizeof(out[count].name));
        out[count].switch_count = slot->switch_count;
        out[count].ready_count = slot->ready_count;
        out[count].max_run_cycles = slot->max_run_cycles;
        out[count].max_ready_cycles = slot->max_ready_cycles;

        if (reset_window != 0u)
        {
            slot->switch_count = 0u;
            slot->ready_count = 0u;
            slot->max_run_cycles = 0u;
            slot->max_ready_cycles = 0u;
        }
        count++;
    }
    taskEXIT_CRITICAL();
    return count;
}

uint32_t tskptt_hskp_trace_get_overflow_count(void)
{
    uint32_t count;
    taskENTER_CRITICAL();
    count = s_trace_slot_overflow;
    taskEXIT_CRITICAL();
    return count;
}
