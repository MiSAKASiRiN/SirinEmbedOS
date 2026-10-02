#include "monitor_internal.h"
#include <string.h>
#include <limits.h>

void SRNOS_MonitorConvert(SRNOS_MonitorADC *a,const uint16_t raw[5],const SRNOS_MonitorCalibration *c,uint64_t start,uint64_t end)
{
    memcpy(a->raw,raw,sizeof(a->raw));
    a->start_us=start; a->completed_us=end; ++a->sequence;
    a->state=SRNOS_MONITOR_ADC_READY; a->valid_fields=0; a->quality=0;
    for (unsigned i=0;i<5;i++) if(raw[i]>=4095) a->quality|=SRNOS_MONITOR_SATURATED;
    if (!c->vref || c->vref>=4095 || !raw[3] || raw[3]>=4095) {
        a->quality|=SRNOS_MONITOR_CAL_INVALID; return;
    }
    /* H723 factory data and ADC3 samples are 12-bit, calibration VREF+=3.3V.
     * Do not use the generic temperature helper which scales input to 16-bit. */
    a->vdda_v=3.3f*(float)c->vref/(float)raw[3];
    if (a->vdda_v<1.62f || a->vdda_v>3.6f) { a->quality|=SRNOS_MONITOR_CAL_INVALID; return; }
    float lsb=a->vdda_v/4095.0f;
    a->vrefint_v=(float)raw[3]*lsb;
    a->valid_fields=SRNOS_MONITOR_VREF_VALID|SRNOS_MONITOR_VDDA_VALID;
    a->current_a=(float)raw[0]*lsb/0.92512f;
    a->voltage_v=(float)raw[1]*lsb*11.0f;
    a->vbat_v=(float)raw[4]*lsb*4.0f;
    if (raw[0]<4095) a->valid_fields|=SRNOS_MONITOR_CURRENT_VALID;
    if (raw[1]<4095) a->valid_fields|=SRNOS_MONITOR_VOLTAGE_VALID;
    if (raw[4]<4095) a->valid_fields|=SRNOS_MONITOR_VBAT_VALID;
    if (a->current_a<0.05f) a->quality|=SRNOS_MONITOR_CURRENT_LOW;
    if (c->temperature_30 && c->temperature_130>c->temperature_30 && c->temperature_130<4095) {
        float normalized=(float)raw[2]*a->vdda_v/3.3f;
        a->temperature_c=30.0f+(normalized-c->temperature_30)*100.0f/(c->temperature_130-c->temperature_30);
        if (raw[2] && raw[2]<4095) a->valid_fields|=SRNOS_MONITOR_TEMPERATURE_VALID;
    } else a->quality|=SRNOS_MONITOR_CAL_INVALID;
}
static uint64_t to_us(uint64_t cycles,uint32_t hz)
{
    return cycles/hz*1000000u+(cycles%hz)*1000000u/hz;
}
void SRNOS_MonitorTaskUpdate(SRNOS_MonitorTask *out,SRNOS_MonitorBaseline *b,const SRNOS_RuntimeSample *s,uint32_t hz)
{
    memset(out,0,sizeof(*out));
    out->slot=s->slot; out->generation=s->generation; out->sampled_us=s->sampled_us;
    out->stack_free_words=UINT16_MAX;
    if (!s->active || !hz) { b->valid=false; return; }
    memcpy(out->name,s->name,sizeof(out->name));
    out->state=s->state; out->priority=s->priority; out->base_priority=s->base_priority;
    out->flags=SRNOS_MONITOR_TASK_ACTIVE|(s->idle?SRNOS_MONITOR_TASK_IDLE:0)|(s->self?SRNOS_MONITOR_TASK_SELF:0);
    out->switch_count=s->switch_count; out->ready_count=s->ready_count;
    out->max_run_us=(uint32_t)to_us(s->max_run_cycles,hz);
    out->max_ready_us=(uint32_t)to_us(s->max_ready_cycles,hz);
    if (b->valid && b->generation==s->generation && s->cycles>b->cycles) {
        uint64_t wall=s->cycles-b->cycles,run=s->runtime-b->runtime;
        out->window_us=to_us(wall,hz); out->runtime_us=to_us(run,hz);
        if(run>wall) run=wall;
        /* Double division avoids overflow after long starvation. 1 ppm resolution. */
        out->cpu_ppm=(uint32_t)((double)run*1000000.0/(double)wall);
        out->flags|=SRNOS_MONITOR_TASK_CPU_VALID;
    }
    b->cycles=s->cycles; b->runtime=s->runtime; b->generation=s->generation; b->valid=true;
}
uint64_t SRNOS_MonitorNextDeadline(uint64_t deadline,uint64_t now,uint32_t *skipped)
{
    uint64_t next=deadline+SRNOS_MONITOR_PERIOD_US;
    if (now>=next) {
        uint64_t missed=(now-next)/SRNOS_MONITOR_PERIOD_US+1;
        *skipped+=(uint32_t)missed; next+=missed*SRNOS_MONITOR_PERIOD_US;
    }
    return next;
}
