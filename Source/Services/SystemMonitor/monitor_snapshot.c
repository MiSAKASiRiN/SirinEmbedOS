#include "monitor_internal.h"
#include "SirinEmbedOS/Diagnostics/Time/Time.h"
#include "FreeRTOS.h"
#include "stm32h7xx.h"
#include "task.h"
static SRNOS_SystemMonitorSnapshot published;
static SRNOS_MonitorTask tasks[SRNOS_MONITOR_MAX_TASKS];
static uint32_t max_copy_cycles;
static void measure(uint32_t begin)
{
    uint32_t cycles=SRNOS_TimeGetElapsedCycles(begin,SRNOS_TimeGetCounter());
    if(cycles>max_copy_cycles) max_copy_cycles=cycles;
}
void SRNOS_MonitorPublish(const SRNOS_SystemMonitorSnapshot *s)
{
    taskENTER_CRITICAL(); uint32_t begin=SRNOS_TimeGetCounter();
    published=*s; measure(begin); taskEXIT_CRITICAL();
}
void SRNOS_MonitorPublishTask(const SRNOS_MonitorTask *s)
{
    taskENTER_CRITICAL(); uint32_t begin=SRNOS_TimeGetCounter();
    tasks[s->slot-1]=*s; measure(begin); taskEXIT_CRITICAL();
}
uint32_t SRNOS_MonitorMaxCopyCycles(void) { return max_copy_cycles; }
bool SRNOS_SystemMonitorGetSnapshot(SRNOS_SystemMonitorSnapshot *out)
{
    if (!out || __get_IPSR()) return false;
    taskENTER_CRITICAL(); uint32_t begin=SRNOS_TimeGetCounter();
    *out=published; measure(begin); taskEXIT_CRITICAL();
    uint64_t now=SRNOS_TimeGetUptimeUs();
    if (out->flags&SRNOS_MONITOR_ENABLED) {
        if (!out->updated_us || now<out->updated_us || now-out->updated_us>SRNOS_MONITOR_STALE_US) {
            out->flags=(out->flags&~SRNOS_MONITOR_CPU_VALID)|SRNOS_MONITOR_STALE;
        }
        if (!out->adc.sequence || now<out->adc.completed_us || now-out->adc.completed_us>SRNOS_MONITOR_STALE_US) {
            out->adc.valid_fields=0; out->adc.quality|=SRNOS_MONITOR_ADC_STALE;
        }
    }
    return true;
}
bool SRNOS_SystemMonitorGetTask(uint8_t slot,SRNOS_MonitorTask *out)
{
    if (!out || !slot || slot>SRNOS_MONITOR_MAX_TASKS || __get_IPSR()) return false;
    taskENTER_CRITICAL(); uint32_t begin=SRNOS_TimeGetCounter();
    *out=tasks[slot-1]; measure(begin); taskEXIT_CRITICAL();
    out->slot=slot;
    uint64_t now=SRNOS_TimeGetUptimeUs();
    if (out->flags&SRNOS_MONITOR_TASK_ACTIVE && (now<out->sampled_us || now-out->sampled_us>SRNOS_MONITOR_STALE_US))
    {
        out->flags=(out->flags&~SRNOS_MONITOR_TASK_CPU_VALID)|SRNOS_MONITOR_TASK_STALE;
        out->cycle.flags=(out->cycle.flags&~(SRNOS_CYCLE_MEASURED|SRNOS_CYCLE_JITTER_VALID))|SRNOS_CYCLE_STALE;
    }
    return true;
}
