#include "FreeRTOS.h"
#include "task.h"
#include "SirinEmbedOS/Services/HostLink/SystemMonitorTelemetry.h"
#include "SirinEmbedOS/Core/C/Endian.h"
#include <string.h>
_Static_assert(sizeof(float)==4, "SystemMonitor wire requires binary32");
static void u16(uint8_t *b,size_t cap,size_t at,uint16_t v) { (void)SRNOS_Endian_StoreLE16(SRNOS_Memory_MutableBytes(b,cap),at,v); }
static void u32(uint8_t *b,size_t cap,size_t at,uint32_t v) { (void)SRNOS_Endian_StoreLE32(SRNOS_Memory_MutableBytes(b,cap),at,v); }
static void u64(uint8_t *b,size_t cap,size_t at,uint64_t v) { (void)SRNOS_Endian_StoreLE64(SRNOS_Memory_MutableBytes(b,cap),at,v); }
static void f32(uint8_t *b,size_t cap,size_t at,float v) { uint32_t bits; memcpy(&bits,&v,4); u32(b,cap,at,bits); }
bool SRNOS_MonitorEncodeSnapshot(const SRNOS_SystemMonitorSnapshot *s,uint8_t *b,size_t cap)
{
    if(!s || !b || cap<SRNOS_MONITOR_ADC_RECORD_BYTES) return false;
    cap=SRNOS_MONITOR_ADC_RECORD_BYTES; memset(b,0,cap);
    const SRNOS_MonitorADC *a=&s->adc;
    b[0]=SRNOS_MONITOR_WIRE_VERSION; b[1]=(uint8_t)a->state;
    u32(b,cap,4,s->sequence); u32(b,cap,8,s->flags); u32(b,cap,12,a->sequence);
    u64(b,cap,16,s->updated_us); u64(b,cap,24,a->start_us); u64(b,cap,32,a->completed_us);
    u32(b,cap,40,a->valid_fields); u32(b,cap,44,a->quality);
    for(unsigned i=0;i<5;i++) u16(b,cap,48+i*2,a->raw[i]);
    f32(b,cap,60,a->current_a); f32(b,cap,64,a->voltage_v); f32(b,cap,68,a->vbat_v);
    f32(b,cap,72,a->vrefint_v); f32(b,cap,76,a->temperature_c); f32(b,cap,80,a->vdda_v);
    const uint32_t counters[]={a->errors,a->timeouts,a->recoveries,s->busy_ppm,s->idle_ppm,
        s->monitor_cpu_ppm,s->task_count,s->max_critical_us,s->max_wake_late_us,s->skipped_cycles,s->slot_overflows};
    for(unsigned i=0;i<11;i++) u32(b,cap,84+4*i,counters[i]);
    return true;
}
bool SRNOS_MonitorEncodeTask(const SRNOS_SystemMonitorSnapshot *s,const SRNOS_MonitorTask *t,uint8_t *b,size_t cap)
{
    if(!s || !t || !b || cap<SRNOS_MONITOR_TASK_RECORD_BYTES) return false;
    cap=SRNOS_MONITOR_TASK_RECORD_BYTES; memset(b,0,cap);
    b[0]=SRNOS_MONITOR_WIRE_VERSION; b[1]=t->slot; b[2]=(uint8_t)t->flags; b[3]=t->state;
    u32(b,cap,4,t->generation); u32(b,cap,8,s->sequence); b[12]=t->priority; b[13]=t->base_priority;
    u16(b,cap,14,t->stack_free_words); u64(b,cap,16,t->sampled_us); u64(b,cap,24,t->window_us); u64(b,cap,32,t->runtime_us);
    u32(b,cap,40,t->cpu_ppm); u32(b,cap,44,t->switch_count); u32(b,cap,48,t->ready_count);
    u32(b,cap,52,t->max_run_us); u32(b,cap,56,t->max_ready_us); memcpy(b+60,t->name,16);
    u64(b,cap,76,s->updated_us); u32(b,cap,84,s->flags); u32(b,cap,88,s->busy_ppm);
    u32(b,cap,92,s->idle_ppm); u32(b,cap,96,s->monitor_cpu_ppm); u32(b,cap,100,s->task_count);
    u32(b,cap,104,s->max_critical_us); u32(b,cap,108,s->slot_overflows);
    return true;
}
bool SRNOS_MonitorEncodeCycle(const SRNOS_SystemMonitorSnapshot *snapshot,const SRNOS_MonitorTask *task,uint8_t *b,size_t cap)
{
    if(!snapshot || !task || !b || cap<SRNOS_MONITOR_CYCLE_RECORD_BYTES)return false;
    cap=SRNOS_MONITOR_CYCLE_RECORD_BYTES;memset(b,0,cap);
    const SRNOS_MonitorCycle *t=&task->cycle;
    b[0]=1;b[1]=task->slot;b[2]=(uint8_t)t->flags;
    u32(b,cap,4,task->generation);memcpy(b+8,task->name,16);
    u64(b,cap,24,t->sampled_us);u64(b,cap,32,t->window_us);
    const uint32_t values[]={t->period_ms,t->completed,t->jitter_samples,
        t->execution_min_us,t->execution_mean_us,t->execution_max_us,
        t->start_late_mean_us,t->start_late_max_us,(uint32_t)t->jitter_min_us,
        (uint32_t)t->jitter_max_us,t->jitter_abs_mean_us,t->deadline_misses,t->skipped,
        t->hook_mean_ns,t->hook_max_ns};
    for(unsigned i=0;i<15;i++)u32(b,cap,40+4*i,values[i]);
    u64(b,cap,100,snapshot->updated_us);return true;
}
typedef struct { uint16_t id; bool enabled, sent; uint32_t last_ms,sequence,failures; uint8_t cursor; } stream_t;
static stream_t streams[3]={{.id=SRNOS_MONITOR_STREAM_TASKS,.cursor=1},{.id=SRNOS_MONITOR_STREAM_ADC,.cursor=1},{.id=SRNOS_MONITOR_STREAM_CYCLES,.cursor=1}};
static uint8_t encoded[4*SRNOS_MONITOR_TASK_RECORD_BYTES];
static bool registered;
static void configure(void *user,bool enable,uint32_t period)
{
    (void)period; stream_t *s=user; s->enabled=enable; s->sent=false; s->cursor=1;
}
static void poll(void *user,uint32_t now)
{
    stream_t *s=user;
    if(!s->enabled || (s->sent && (uint32_t)(now-s->last_ms)<1000u)) return;
    SRNOS_SystemMonitorSnapshot snapshot;
    if(!SRNOS_SystemMonitorGetSnapshot(&snapshot) || !snapshot.sequence) return;
    s->sent=true; s->last_ms=now;
    unsigned count=0,bytes=0;
    if(s->id==SRNOS_MONITOR_STREAM_ADC) {
        (void)SRNOS_MonitorEncodeSnapshot(&snapshot,encoded,sizeof(encoded)); count=1; bytes=SRNOS_MONITOR_ADC_RECORD_BYTES;
    } else {
        /* At most 64 small per-slot reads, no table-wide lock; skip inactive.
         * Four active tasks per report => every active task within 16 reports. */
        for(unsigned scanned=0;scanned<SRNOS_MONITOR_MAX_TASKS && count<4;scanned++) {
            SRNOS_MonitorTask task;
            (void)SRNOS_SystemMonitorGetTask(s->cursor,&task);
            s->cursor=s->cursor==SRNOS_MONITOR_MAX_TASKS?1:s->cursor+1;
            if(!(task.flags&SRNOS_MONITOR_TASK_ACTIVE)) continue;
            if(s->id==SRNOS_MONITOR_STREAM_CYCLES) (void)SRNOS_MonitorEncodeCycle(&snapshot,&task,encoded+bytes,sizeof(encoded)-bytes);
            else (void)SRNOS_MonitorEncodeTask(&snapshot,&task,encoded+bytes,sizeof(encoded)-bytes);
            bytes+=SRNOS_MONITOR_TASK_RECORD_BYTES; ++count;
        }
        if(!count) {
            SRNOS_MonitorTask empty={.stack_free_words=UINT16_MAX};
            if(s->id==SRNOS_MONITOR_STREAM_CYCLES) (void)SRNOS_MonitorEncodeCycle(&snapshot,&empty,encoded,sizeof(encoded));
            else (void)SRNOS_MonitorEncodeTask(&snapshot,&empty,encoded,sizeof(encoded));
            bytes=SRNOS_MONITOR_TASK_RECORD_BYTES; count=1;
        }
    }
    /* uint16 wire sample_period cannot represent 1 second. Zero means
     * irregular/explicit timestamps; subscription period remains 1,000,000. */
    if(SRNOS_HostLinkPublishTelemetry(s->id,++s->sequence,(uint32_t)snapshot.updated_us,0,(uint8_t)count,encoded,bytes)!=SRNOS_HOST_LINK_OK)
        ++s->failures; /* never retry-spin or retain a backlog */
}
SRNOS_HostLinkResult SRNOS_SystemMonitorTelemetryRegister(SRNOS_HostLinkRegistration *link,void *unused)
{
    (void)link; (void)unused;
    if(registered) return SRNOS_HOST_LINK_ERR_PARAM;
    for(unsigned i=0;i<3;i++) {
        const SRNOS_HostLinkTelemetryConfig c={.stream_id=streams[i].id,
            .max_sample_size=i==1?SRNOS_MONITOR_ADC_RECORD_BYTES:sizeof(encoded),
            .required_period_us=SRNOS_MONITOR_PERIOD_US,.policy=SRNOS_HOST_LINK_LATEST,
            .on_config=configure,.poll=poll,.user=&streams[i]};
        SRNOS_HostLinkResult result=SRNOS_HostLinkRegisterTelemetryEx(&c);
        if(result!=SRNOS_HOST_LINK_OK) return result;
    }
    registered=true; return SRNOS_HOST_LINK_OK;
}
