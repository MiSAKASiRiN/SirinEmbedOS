#include "cycle_internal.h"
#include "thread_internal.h"
#include "monitor_internal.h"
#include "Devices/monitor_adc_board.h"
#include "SirinEmbedOS/Diagnostics/Time/Time.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"

static SRNOS_SystemMonitorSnapshot state;
static SRNOS_MonitorBaseline baselines[SRNOS_MONITOR_MAX_TASKS];
static bool prepared,started,enabled,calibrated,quarantined;
static SRNOS_MonitorCalibration calibration;
/* Ceil to ticks, never busy-wait. Period and conversion timeout are short. */
static uint32_t ticks_for(uint64_t us)
{
    uint64_t ticks=(us*osKernelGetTickFreq()+999999)/1000000;
    return ticks?(uint32_t)ticks:1u;
}
static void adc_failure(bool timeout)
{
    ++state.adc.errors;
    if(timeout) ++state.adc.timeouts;
    state.adc.valid_fields=0;
    state.adc.state=SRNOS_MONITOR_ADC_ERROR;
    if (!SRNOS_BoardADCStop()) {
        quarantined=true; state.adc.state=SRNOS_MONITOR_ADC_QUARANTINED;
    } else { ++state.adc.recoveries; calibrated=false; }
}
static void sample_adc(void)
{
    if (quarantined) return;
    if (!calibrated) {
        uint16_t c[3];
        if (!SRNOS_BoardADCInitialize(osThreadGetId(),c)) { adc_failure(false); return; }
        calibration=(SRNOS_MonitorCalibration){c[0],c[1],c[2]}; calibrated=true;
    }
    if (!SRNOS_BoardADCStart()) { adc_failure(false); return; }
    (void)osThreadFlagsWait(SRNOS_BOARD_ADC_EVENT,osFlagsWaitAny,ticks_for(10000));
    SRNOS_BoardADCResult result;
    bool completed=SRNOS_BoardADCTake(&result);
    /* A delayed task can observe an on-time IRQ after the wait's deadline.
     * Its IRQ timestamp, rather than our scheduling time, decides timeout. */
    bool timeout=!completed || result.end_us<result.start_us || result.end_us-result.start_us>10000;
    if (timeout || result.error) { adc_failure(timeout); return; }
    if (!SRNOS_BoardADCStop()) {
        quarantined=true; ++state.adc.errors; state.adc.valid_fields=0;
        state.adc.state=SRNOS_MONITOR_ADC_QUARANTINED; return;
    }
    SRNOS_MonitorConvert(&state.adc,result.raw,&calibration,result.start_us,result.end_us);
}
static void sample_tasks(void)
{
    state.task_count=0; state.flags&=~SRNOS_MONITOR_CPU_VALID;
    uint32_t hz=SRNOS_TimeGetCpuHz();
    for (uint8_t slot=1;slot<=SRNOS_MONITOR_MAX_TASKS;slot++) {
        SRNOS_RuntimeSample raw;
        SRNOS_MonitorTask record;
        SRNOS_RuntimeSampleSlot(slot,&raw);
        SRNOS_MonitorTaskUpdate(&record,&baselines[slot-1],&raw,hz);
        if(raw.active) SRNOS_CycleSample(slot,raw.generation,&record.cycle);
        if(record.flags&SRNOS_MONITOR_TASK_ACTIVE) ++state.task_count;
        if(record.flags&SRNOS_MONITOR_TASK_CPU_VALID) {
            if(record.flags&SRNOS_MONITOR_TASK_IDLE) {
                state.idle_ppm=record.cpu_ppm; state.busy_ppm=1000000u-record.cpu_ppm;
                state.flags|=SRNOS_MONITOR_CPU_VALID;
            }
            if(record.flags&SRNOS_MONITOR_TASK_SELF) state.monitor_cpu_ppm=record.cpu_ppm;
        }
        SRNOS_MonitorPublishTask(&record);
        if(slot%4==0 && slot<SRNOS_MONITOR_MAX_TASKS) (void)osDelay(1);
    }
    uint32_t maximum=SRNOS_RuntimeMaxCriticalCycles();
    if(SRNOS_CycleMaxCriticalCycles()>maximum) maximum=SRNOS_CycleMaxCriticalCycles();
    if(SRNOS_MonitorMaxCopyCycles()>maximum) maximum=SRNOS_MonitorMaxCopyCycles();
    if(hz) state.max_critical_us=(uint32_t)(((uint64_t)maximum*1000000u+hz-1)/hz);
    state.slot_overflows=SRNOS_RuntimeOverflows();
}
typedef struct { uint64_t deadline; } monitor_loop_t;
static bool monitor_wait(void *arg)
{
    monitor_loop_t *c=arg;
    for(;;) {
        uint64_t now=SRNOS_TimeGetUptimeUs();if(now>=c->deadline)return true;
        (void)osDelay(ticks_for(c->deadline-now));
    }
}
static void monitor_step(void *arg)
{
    monitor_loop_t *c=arg;uint64_t now=SRNOS_TimeGetUptimeUs(),late=now-c->deadline;
    if(late>state.max_wake_late_us)state.max_wake_late_us=late>UINT32_MAX?UINT32_MAX:(uint32_t)late;
    uint64_t cycle_start=now;
    sample_adc();sample_tasks();now=SRNOS_TimeGetUptimeUs();
    c->deadline=SRNOS_MonitorNextDeadline(c->deadline,now,&state.skipped_cycles);
    if(c->deadline<cycle_start+SRNOS_MONITOR_PERIOD_US)c->deadline=cycle_start+SRNOS_MONITOR_PERIOD_US;
    state.updated_us=now;++state.sequence;SRNOS_MonitorPublish(&state);
}
static void worker(void *unused)
{
    (void)unused;monitor_loop_t context={SRNOS_TimeGetUptimeUs()};
    /* This service's microsecond retry/deadline policy is retained privately. */
    const SRNOS_InternalLoop loop={monitor_step,monitor_wait,&context,1000,true};
    SRNOS_KernelRunLoop(&loop);
}
bool SRNOS_SystemMonitorPrepare(bool enable)
{
    if(prepared || osKernelGetState()!=osKernelInactive) return false;
    SRNOS_TimeInit(0); if(!SRNOS_TimeIsReady()) return false;
    enabled=enable; prepared=true;
    state.flags=enable?SRNOS_MONITOR_ENABLED:0;
    state.adc.state=enable?SRNOS_MONITOR_ADC_STARTING:SRNOS_MONITOR_ADC_DISABLED;
    SRNOS_MonitorPublish(&state); return true;
}
uint32_t SRNOS_SystemMonitorHeapBudget(bool enabled)
{
    return enabled ? SRNOS_MONITOR_WORKER_STACK_BYTES + SRNOS_MONITOR_WORKER_RESERVE_BYTES : 0u;
}
bool SRNOS_SystemMonitorStart(void)
{
    if(!prepared || started || osKernelGetState()!=osKernelReady) return false;
    if(enabled) {
        const osThreadAttr_t attr={.name="SRNOS_Monitor",.stack_size=SRNOS_MONITOR_WORKER_STACK_BYTES,.priority=osPriorityLow};
        if(!SRNOS_KernelThreadNew(worker,NULL,&attr)) return false;
    }
    started=true; return true;
}
