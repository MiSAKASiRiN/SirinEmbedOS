#include "cycle_internal.h"
#include "runtime_internal.h"
#include "SirinEmbedOS/Diagnostics/Time/Time.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#if defined(__arm__) || defined(__thumb__)
#include "stm32h7xx.h"
/* Kernel startup initializes DWT before enabling hooks. Timing their cost
 * must not repeatedly run the public lazy-initialization path. */
static inline uint32_t counter(void) {return DWT->CYCCNT;}
#else
static inline uint32_t counter(void) {return SRNOS_TimeGetCounter();}
#endif
typedef struct {
    uint64_t window,execution_sum,execution_min,execution_max,late_sum,late_max,jitter_abs_sum,hook_sum;
    int64_t jitter_min,jitter_max;
    uint32_t generation,period_ms,completed,jitter_count,deadline_misses,skipped,hook_max;
    bool active;
} accumulator_t;
static accumulator_t *slots;
_Static_assert(sizeof(accumulator_t)*SRNOS_RUNTIME_MAX_TASKS<=8192,"update cycle startup budget");
static bool enabled,anchor_valid;
static uint32_t max_critical_cycles;
static uint32_t enter(void) { taskENTER_CRITICAL();return counter(); }
static void leave(uint32_t began) {
    uint32_t elapsed=counter()-began;
    if(elapsed>max_critical_cycles)max_critical_cycles=elapsed;
    taskEXIT_CRITICAL();
}
uint32_t SRNOS_CycleMaxCriticalCycles(void) {return max_critical_cycles;}
static uint64_t tick_anchor;
static uint32_t tick_anchor_count,cycles_per_tick;
static uint64_t observed_cycles;
/* Called with the tick masked. The existing per-tick trace refreshes this
 * anchor even when tasks sleep for multiple CYCCNT wraps. Hook reads need
 * neither public lazy initialization nor a second nested PRIMASK section. */
static uint64_t now_locked(void)
{
    observed_cycles+=(uint32_t)(counter()-(uint32_t)observed_cycles);
    return observed_cycles;
}
bool SRNOS_CycleConfigure(bool value)
{
    if(value && !slots) {
        slots=pvPortMalloc(sizeof(*slots)*SRNOS_RUNTIME_MAX_TASKS);
        if(!slots)return false;
        memset(slots,0,sizeof(*slots)*SRNOS_RUNTIME_MAX_TASKS);
    }
    enabled=value;cycles_per_tick=SRNOS_TimeGetCpuHz()/configTICK_RATE_HZ;return true;
}
void SRNOS_CycleTick(uint64_t cycles,uint32_t tick)
{
    if(!enabled)return;
    observed_cycles=cycles;
    /* First tick anchors ideal releases to the scheduler clock, not to a
     * task's arbitrary sub-tick creation phase. Fixed board clock only. */
    if(enabled && !anchor_valid){tick_anchor=cycles;tick_anchor_count=tick;anchor_valid=true;}
    /* Refresh by exact tick multiples before half-range wrap. */
    if(anchor_valid && (uint32_t)(tick-tick_anchor_count)>1000000u) {
        tick_anchor+=(uint64_t)(uint32_t)(tick-tick_anchor_count)*cycles_per_tick;tick_anchor_count=tick;
    }
}
void SRNOS_CycleAttach(SRNOS_CycleContext *c,uint32_t period)
{
    memset(c,0,sizeof(*c));c->period_ms=period;
    if(!enabled || !SRNOS_RuntimeCurrentIdentity(&c->slot,&c->generation))return;
    c->enabled=true;uint64_t now=SRNOS_TimeGetUptimeCycles64();
    uint32_t critical_begin=enter();
    if(!anchor_valid)observed_cycles=now;
    accumulator_t *s=&slots[c->slot-1];memset(s,0,sizeof(*s));
    s->generation=c->generation;s->period_ms=period;s->window=now;s->active=true;leave(critical_begin);
}
void SRNOS_CycleBegin(SRNOS_CycleContext *c,uint32_t release_tick)
{
    if(!c->enabled)return;
    uint32_t cost=counter(),critical_begin=enter();
    c->began=now_locked();
    uint64_t anchor=tick_anchor;uint32_t count=tick_anchor_count;bool valid=anchor_valid;
    leave(critical_begin);
    if(c->period_ms) {
        int64_t delta=(int64_t)(int32_t)(release_tick-count)*cycles_per_tick;
        c->release=valid?(uint64_t)((int64_t)anchor+delta):c->began;
    }
    c->begin_cost=counter()-cost;
}
void SRNOS_CycleEnd(SRNOS_CycleContext *c)
{
    if(!c->enabled)return;
    uint32_t cost=counter(),critical_begin=enter();
    uint64_t now=now_locked(),duration=now-c->began;
    accumulator_t *s=&slots[c->slot-1];
    if(s->generation==c->generation) {
        if(!s->completed || duration<s->execution_min)s->execution_min=duration;
        if(duration>s->execution_max)s->execution_max=duration;
        s->execution_sum+=duration;++s->completed;
        if(c->period_ms) {
            uint64_t late=c->began>c->release?c->began-c->release:0;
            s->late_sum+=late;if(late>s->late_max)s->late_max=late;
            if(now>c->release+(uint64_t)c->period_ms*cycles_per_tick)++s->deadline_misses;
            if(c->have_previous){
                int64_t jitter=(int64_t)(c->began-c->previous)-(int64_t)c->period_ms*cycles_per_tick;
                if(!s->jitter_count || jitter<s->jitter_min)s->jitter_min=jitter;
                if(!s->jitter_count || jitter>s->jitter_max)s->jitter_max=jitter;
                s->jitter_abs_sum+=(uint64_t)(jitter<0?-jitter:jitter);++s->jitter_count;
            }
        }
        uint32_t overhead=c->begin_cost+counter()-cost;
        s->hook_sum+=overhead;if(overhead>s->hook_max)s->hook_max=overhead;
    }
    leave(critical_begin);c->previous=c->began;c->have_previous=true;
}
void SRNOS_CycleSkipped(SRNOS_CycleContext *c,uint32_t count)
{if(c->enabled && count){uint32_t critical_begin=enter();slots[c->slot-1].skipped+=count;leave(critical_begin);}}
static uint32_t units(uint64_t cycles,uint32_t scale,uint32_t hz,uint32_t *flags)
{
    uint64_t n=(cycles/hz)*scale+(cycles%hz)*scale/hz;
    if(n>UINT32_MAX){*flags|=SRNOS_CYCLE_SATURATED;return UINT32_MAX;}return (uint32_t)n;
}
static int32_t signed_us(int64_t cycles,uint32_t hz,uint32_t *flags)
{
    bool negative=cycles<0;uint32_t n=units((uint64_t)(negative?-cycles:cycles),1000000,hz,flags);
    if(n>INT32_MAX){*flags|=SRNOS_CYCLE_SATURATED;n=INT32_MAX;}return negative?-(int32_t)n:(int32_t)n;
}
void SRNOS_CycleSample(uint8_t slot,uint32_t generation,SRNOS_MonitorCycle *out)
{
    memset(out,0,sizeof(*out));out->slot=slot;out->generation=generation;
    if(!slot || slot>SRNOS_RUNTIME_MAX_TASKS || !enabled)return;
    accumulator_t copy;
    uint32_t critical_begin=enter();uint64_t now=SRNOS_TimeGetUptimeCycles64();accumulator_t *s=&slots[slot-1];
    if(!s->active || s->generation!=generation){leave(critical_begin);return;}
    copy=*s;memset(s,0,sizeof(*s));s->active=true;s->generation=copy.generation;s->period_ms=copy.period_ms;s->window=now;
    leave(critical_begin);
    uint32_t hz=SRNOS_TimeGetCpuHz();if(!hz)return;
    out->sampled_us=(now/hz)*1000000u+(now%hz)*1000000u/hz;out->window_us=(now-copy.window)/hz*1000000u+(now-copy.window)%hz*1000000u/hz;
    out->flags=SRNOS_CYCLE_ACTIVE;out->period_ms=copy.period_ms;
    if(copy.period_ms)out->flags|=SRNOS_CYCLE_PERIODIC;
    out->completed=copy.completed;out->jitter_samples=copy.jitter_count;out->deadline_misses=copy.deadline_misses;out->skipped=copy.skipped;
    if(copy.completed) {
        out->flags|=SRNOS_CYCLE_MEASURED;
        out->execution_min_us=units(copy.execution_min,1000000,hz,&out->flags);
        out->execution_mean_us=units(copy.execution_sum/copy.completed,1000000,hz,&out->flags);
        out->execution_max_us=units(copy.execution_max,1000000,hz,&out->flags);
        out->start_late_mean_us=units(copy.late_sum/copy.completed,1000000,hz,&out->flags);
        out->start_late_max_us=units(copy.late_max,1000000,hz,&out->flags);
        out->hook_mean_ns=units(copy.hook_sum/copy.completed,1000000000,hz,&out->flags);
        out->hook_max_ns=units(copy.hook_max,1000000000,hz,&out->flags);
    }
    if(copy.jitter_count){out->flags|=SRNOS_CYCLE_JITTER_VALID;
        out->jitter_min_us=signed_us(copy.jitter_min,hz,&out->flags);out->jitter_max_us=signed_us(copy.jitter_max,hz,&out->flags);
        out->jitter_abs_mean_us=units(copy.jitter_abs_sum/copy.jitter_count,1000000,hz,&out->flags);}
}
