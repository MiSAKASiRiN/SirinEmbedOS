#include "runtime_internal.h"
#include "FreeRTOS.h"
#include "task.h"

#include "SirinEmbedOS/Diagnostics/Time/Time.h"
#include <string.h>

_Static_assert(configMAX_TASK_NAME_LEN == 16, "Monitor task name contract");

typedef struct
{
    TaskHandle_t handle;
    UBaseType_t task_number;
    uint32_t generation;
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
static hskp_trace_slot_t s_trace_slots[SRNOS_RUNTIME_MAX_TASKS + 1u];
static volatile uint32_t s_trace_slot_overflow;
static uint64_t s_runtime_counter_base;
static uint8_t s_runtime_counter_initialized;

static UBaseType_t hskp_trace_allocate_slot(void)
{
    for (UBaseType_t slot_number = 1u;
         slot_number <= SRNOS_RUNTIME_MAX_TASKS;
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
    if ((task_number == 0u) || (task_number > SRNOS_RUNTIME_MAX_TASKS))
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

void SRNOS_RuntimeStatsInit(void)
{
    if (s_runtime_counter_initialized != 0u)
    {
        return;
    }

    SRNOS_TimeInit(0u);
    s_runtime_counter_base = SRNOS_TimeGetUptimeCycles64();
    s_runtime_counter_initialized = 1u;
}

uint64_t SRNOS_RuntimeStatsCounter(void)
{
    if (s_runtime_counter_initialized == 0u)
    {
        SRNOS_RuntimeStatsInit();
    }
    return SRNOS_TimeGetUptimeCycles64() - s_runtime_counter_base;
}

void SRNOS_RuntimeTraceCreate(void *task_handle)
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
    if (++slot->generation == 0u) ++slot->generation;
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

void SRNOS_RuntimeTraceDelete(void *task_handle)
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

void SRNOS_RuntimeTraceReady(void *task_handle)
{
    hskp_trace_slot_t *slot = hskp_trace_slot_from_handle(task_handle);
    if ((slot == NULL) || (slot->active == 0u) || !SRNOS_TimeIsReady())
    {
        return;
    }

    slot->ready_counter = SRNOS_TimeGetCounter();
    slot->ready_valid = 1u;
    slot->ready_count++;
}

void SRNOS_RuntimeTraceIn(void)
{
    TaskHandle_t handle = xTaskGetCurrentTaskHandle();
    hskp_trace_slot_t *slot = hskp_trace_slot_from_handle(handle);
    if ((slot == NULL) || (slot->active == 0u) || !SRNOS_TimeIsReady())
    {
        return;
    }

    uint32_t now = SRNOS_TimeGetCounter();
    if (slot->ready_valid != 0u)
    {
        uint32_t latency = SRNOS_TimeGetElapsedCycles(slot->ready_counter, now);
        hskp_trace_update_max(&slot->max_ready_cycles, latency);
        slot->ready_valid = 0u;
    }
    slot->switch_in_counter = now;
    slot->running = 1u;
    slot->switch_count++;
}

void SRNOS_RuntimeTraceOut(void)
{
    TaskHandle_t handle = xTaskGetCurrentTaskHandle();
    hskp_trace_slot_t *slot = hskp_trace_slot_from_handle(handle);
    if ((slot == NULL) || (slot->active == 0u) ||
        (slot->running == 0u) || !SRNOS_TimeIsReady())
    {
        return;
    }

    uint32_t now = SRNOS_TimeGetCounter();
    uint32_t run_cycles = SRNOS_TimeGetElapsedCycles(slot->switch_in_counter, now);
    hskp_trace_update_max(&slot->max_run_cycles, run_cycles);
    slot->running = 0u;
}

static uint32_t max_critical_cycles;
void SRNOS_RuntimeSampleSlot(uint8_t number, SRNOS_RuntimeSample *out)
{
    if (!out || !number || number > SRNOS_RUNTIME_MAX_TASKS) return;
    memset(out, 0, sizeof(*out));
    out->slot = number;
    if (!s_runtime_counter_initialized) SRNOS_RuntimeStatsInit();
    taskENTER_CRITICAL();
    const uint32_t begin = SRNOS_TimeGetCounter();
    hskp_trace_slot_t *slot = &s_trace_slots[number];
    out->generation = slot->generation;
    const uint64_t absolute_cycles = SRNOS_TimeGetUptimeCycles64();
    out->cycles = absolute_cycles - s_runtime_counter_base;
    if (slot->active && slot->handle) {
        TaskStatus_t status;
        vTaskGetInfo(slot->handle, &status, pdFALSE, eInvalid);
        out->active = true;
        out->idle = slot->handle == xTaskGetIdleTaskHandle();
        out->self = slot->handle == xTaskGetCurrentTaskHandle();
        out->state = (uint8_t)status.eCurrentState;
        out->priority = (uint8_t)status.uxCurrentPriority;
        out->base_priority = (uint8_t)status.uxBasePriority;
        out->runtime = status.ulRunTimeCounter;
        /* FreeRTOS commits the running task's counter only on switch-out.
         * Sample its committed value consistently; the next window includes
         * this short sampling slice. Do not mix two different time origins. */
        memcpy(out->name, slot->name, sizeof(out->name));
        out->switch_count = slot->switch_count;
        out->ready_count = slot->ready_count;
        out->max_run_cycles = slot->max_run_cycles;
        out->max_ready_cycles = slot->max_ready_cycles;
        slot->switch_count = slot->ready_count = 0;
        slot->max_run_cycles = slot->max_ready_cycles = 0;
    }
    uint32_t elapsed = SRNOS_TimeGetElapsedCycles(begin, SRNOS_TimeGetCounter());
    if (elapsed > max_critical_cycles) max_critical_cycles = elapsed;
    taskEXIT_CRITICAL();
    const uint32_t hz = SRNOS_TimeGetCpuHz();
    if (hz) out->sampled_us = (absolute_cycles/hz)*1000000u + (absolute_cycles%hz)*1000000u/hz;
}
uint32_t SRNOS_RuntimeOverflows(void) { return s_trace_slot_overflow; }
uint32_t SRNOS_RuntimeMaxCriticalCycles(void) { return max_critical_cycles; }

bool SRNOS_RuntimeCurrentIdentity(uint8_t *number,uint32_t *generation)
{
    bool found=false;taskENTER_CRITICAL();
    TaskHandle_t task=xTaskGetCurrentTaskHandle();
    hskp_trace_slot_t *slot=hskp_trace_slot_from_handle(task);
    if(slot && slot->active && slot->handle==task){*number=(uint8_t)slot->task_number;*generation=slot->generation;found=true;}
    taskEXIT_CRITICAL();return found;
}
