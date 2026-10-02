#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../../../../Source/Services/SystemMonitor/monitor_internal.h"
#include "../../../../Source/Diagnostics/Runtime/runtime_trace.c"
#include "../../../../Source/Services/SystemMonitor/monitor_snapshot.c"
#include "../../../../Source/Services/SystemMonitor/monitor_service.c"
#include "../../../../Source/Services/HostLink/system_monitor_telemetry.c"
static uint64_t clock_us=1000000;
static unsigned lock_depth,largest_lock,delays,created,publications;
uint32_t fake_ipsr;
static osKernelState_t kernel_state=osKernelInactive;
typedef struct {uint32_t number;const char *name;uint64_t runtime;bool deleted;} fake_task;
static fake_task idle={0,"IDLE",0,false},self={0,"SRNOS_Monitor",0,false},other={0,"IMU",0,false};
static fake_task *current=&self;
void test_enter(void) {if(++lock_depth>largest_lock)largest_lock=lock_depth;}
void test_exit(void) {assert(lock_depth);--lock_depth;}
void SRNOS_TimeInit(uint32_t x) {(void)x;}
uint32_t SRNOS_TimeIsReady(void) {return 1;}
uint32_t SRNOS_TimeGetCpuHz(void) {return 1000000;}
uint32_t SRNOS_TimeGetCounter(void) {return (uint32_t)clock_us;}
uint64_t SRNOS_TimeGetUptimeUs(void) {return clock_us;}
uint64_t SRNOS_TimeGetUptimeCycles64(void) {return clock_us;}
uint32_t SRNOS_TimeGetElapsedCycles(uint32_t a,uint32_t b) {return b-a;}
UBaseType_t uxTaskGetTaskNumber(TaskHandle_t h) {assert(!((fake_task *)h)->deleted);return ((fake_task *)h)->number;}
void vTaskSetTaskNumber(TaskHandle_t h,UBaseType_t n) {((fake_task *)h)->number=n;}
const char *pcTaskGetName(TaskHandle_t h) {return ((fake_task *)h)->name;}
TaskHandle_t xTaskGetCurrentTaskHandle(void) {return current;}
TaskHandle_t xTaskGetIdleTaskHandle(void) {return &idle;}
void vTaskGetInfo(TaskHandle_t h,TaskStatus_t *s,BaseType_t stack,eTaskState state_arg)
{
    assert(lock_depth==1 && !stack && state_arg==eInvalid);
    fake_task *t=h;assert(!t->deleted);
    *s=(TaskStatus_t){t->runtime,t==current?eRunning:eBlocked,8,8};
}
osKernelState_t osKernelGetState(void) {return kernel_state;}
osThreadId_t osThreadGetId(void) {return &self;}
uint32_t osKernelGetTickFreq(void) {return 1000;}
osThreadId_t osThreadNew(void (*fn)(void *),void *arg,const osThreadAttr_t *a)
{
    (void)fn;(void)arg;assert(!strcmp(a->name,"SRNOS_Monitor") && a->stack_size==4096 && a->priority==osPriorityLow);++created;return &self;
}
osStatus_t osDelay(uint32_t ticks) {assert(!lock_depth && ticks==1);++delays;clock_us+=ticks*1000;return 0;}
static bool adc_init_ok=true,adc_start_ok=true,adc_take_ok=true,adc_stop_ok=true,adc_error;
static unsigned stops,attempts;
static uint64_t adc_elapsed=300,processing_delay=100;
bool SRNOS_BoardADCInitialize(osThreadId_t t,uint16_t cal[3]) {assert(t && !lock_depth);cal[0]=1500;cal[1]=800;cal[2]=1100;return adc_init_ok;}
bool SRNOS_BoardADCStart(void) {++attempts;return adc_start_ok;}
bool SRNOS_BoardADCStop(void) {assert(!lock_depth);++stops;return adc_stop_ok;}
bool SRNOS_BoardADCTake(SRNOS_BoardADCResult *r)
{
    *r=(SRNOS_BoardADCResult){{1148,2707,950,1500,1024},clock_us-processing_delay-adc_elapsed,clock_us-processing_delay,adc_error};
    return adc_take_ok;
}
uint32_t osThreadFlagsWait(uint32_t a,uint32_t b,uint32_t t) {(void)a;(void)b;assert(t==10 && !lock_depth);clock_us+=adc_elapsed+processing_delay;return 1;}
static SRNOS_HostLinkTelemetryConfig configs[3];
static unsigned config_count;
static uint8_t last_payload[448];static size_t last_size;static uint16_t last_id;static uint8_t last_count;
static SRNOS_HostLinkResult publish_result=SRNOS_HOST_LINK_OK;
SRNOS_HostLinkResult SRNOS_HostLinkRegisterTelemetryEx(const SRNOS_HostLinkTelemetryConfig *c)
{
    assert(config_count<3 && c->policy==SRNOS_HOST_LINK_LATEST && c->required_period_us==1000000);
    configs[config_count++]=*c;return SRNOS_HOST_LINK_OK;
}
SRNOS_HostLinkResult SRNOS_HostLinkPublishTelemetry(uint16_t id,uint32_t seq,uint32_t device,uint16_t period,uint8_t count,const uint8_t *p,size_t n)
{
    (void)seq;(void)device;assert(!period && n<=448 && count<=4 && n/count<=255);
    ++publications;last_id=id;last_count=count;last_size=n;memcpy(last_payload,p,n);return publish_result;
}
static void conversion_tests(void)
{
    SRNOS_MonitorADC a={0};SRNOS_MonitorCalibration c={1500,800,1100};
    uint16_t raw[]={1148,2707,950,1500,1024};
    SRNOS_MonitorConvert(&a,raw,&c,10,20);
    assert(a.valid_fields==63 && a.sequence==1 && a.completed_us==20);
    assert(fabsf(a.current_a-1)<0.001 && fabsf(a.voltage_v-24)<0.01);
    assert(fabsf(a.vbat_v-3.3008f)<0.001 && fabsf(a.temperature_c-80)<0.01);
    assert(fabsf(a.vdda_v-3.3f)<0.001);
    raw[0]=0;SRNOS_MonitorConvert(&a,raw,&c,30,40);assert(a.quality&SRNOS_MONITOR_CURRENT_LOW);
    raw[1]=4095;SRNOS_MonitorConvert(&a,raw,&c,30,40);assert(!(a.valid_fields&2) && a.quality&2);
    raw[3]=0;SRNOS_MonitorConvert(&a,raw,&c,30,40);assert(!a.valid_fields && a.quality&4);
    raw[3]=1500;c.temperature_130=800;SRNOS_MonitorConvert(&a,raw,&c,30,40);assert(!(a.valid_fields&16));
    c.vref=4095;SRNOS_MonitorConvert(&a,raw,&c,30,40);assert(!a.valid_fields);
    /* Same physical 80C at a 3.0V reference; VDDA correction must remove it. */
    c=(SRNOS_MonitorCalibration){1500,800,1100};raw[3]=1650;raw[2]=1045;
    SRNOS_MonitorConvert(&a,raw,&c,30,40);assert(fabsf(a.temperature_c-80)<0.01);
    uint32_t skipped=0;assert(SRNOS_MonitorNextDeadline(1000000,4500000,&skipped)==5000000 && skipped==3);
}
static void trace_tests(void)
{
    SRNOS_RuntimeTraceCreate(&idle);SRNOS_RuntimeTraceCreate(&self);SRNOS_RuntimeTraceCreate(&other);
    current=&other;clock_us=UINT32_MAX-20ULL;SRNOS_RuntimeTraceReady(&other);
    clock_us+=30;SRNOS_RuntimeTraceIn();clock_us+=70;SRNOS_RuntimeTraceOut();
    SRNOS_RuntimeSample r;SRNOS_RuntimeSampleSlot(other.number,&r);
    assert(r.max_ready_cycles==30 && r.max_run_cycles==70 && r.switch_count==1);
    uint8_t slot=other.number;uint32_t gen=r.generation;
    SRNOS_MonitorBaseline b={0};SRNOS_MonitorTask t;
    SRNOS_MonitorTaskUpdate(&t,&b,&r,1000000);assert(!(t.flags&2));
    r.cycles+=2000000;r.runtime+=500000;r.sampled_us+=2000000;
    SRNOS_MonitorTaskUpdate(&t,&b,&r,1000000);assert(t.cpu_ppm==250000 && t.window_us==2000000 && t.runtime_us==500000);
    SRNOS_RuntimeTraceDelete(&other);other.deleted=true;SRNOS_RuntimeSampleSlot(slot,&r);assert(!r.active);
    other.deleted=false;SRNOS_RuntimeTraceCreate(&other);SRNOS_RuntimeSampleSlot(slot,&r);
    assert(r.generation!=gen);SRNOS_MonitorTaskUpdate(&t,&b,&r,1000000);assert(!(t.flags&2));
    b=(SRNOS_MonitorBaseline){10,UINT64_MAX-50,r.generation,true};r.cycles=110;r.runtime=49;
    SRNOS_MonitorTaskUpdate(&t,&b,&r,1000000);assert(t.runtime_us==100 && t.cpu_ppm==1000000);
    /* Fill the remaining slots, observe bounded overflow, then delete them.
     * No sample may dereference a deleted handle, even before Idle frees it. */
    fake_task extra[62]={0};
    for(unsigned i=0;i<62;i++){extra[i].name="extra";SRNOS_RuntimeTraceCreate(&extra[i]);}
    assert(SRNOS_RuntimeOverflows()==1 && extra[61].number==0);
    for(unsigned i=0;i<61;i++){
        uint8_t n=(uint8_t)extra[i].number;
        SRNOS_RuntimeTraceDelete(&extra[i]);extra[i].deleted=true;
        SRNOS_RuntimeSampleSlot(n,&r);assert(!r.active);
    }
    current=&self;assert(largest_lock==1 && !lock_depth);
}
static void service_tests(void)
{
    assert(SRNOS_SystemMonitorPrepare(true));assert(!SRNOS_SystemMonitorPrepare(true));
    kernel_state=osKernelReady;assert(SRNOS_SystemMonitorStart());assert(created==1);assert(!SRNOS_SystemMonitorStart());
    kernel_state=osKernelRunning;
    processing_delay=50000;sample_adc();assert(state.adc.state==SRNOS_MONITOR_ADC_READY && state.adc.sequence==1 && !state.adc.timeouts);
    uint64_t stamp=state.adc.completed_us;float voltage=state.adc.voltage_v;
    adc_elapsed=11000;sample_adc();assert(state.adc.timeouts==1 && !state.adc.valid_fields && state.adc.completed_us==stamp && state.adc.voltage_v==voltage);
    adc_elapsed=300;adc_take_ok=false;sample_adc();assert(state.adc.timeouts==2);adc_take_ok=true;
    adc_error=true;sample_adc();assert(state.adc.errors==3 && state.adc.timeouts==2);adc_error=false;
    adc_init_ok=false;sample_adc();assert(state.adc.errors==4);adc_init_ok=true;
    adc_start_ok=false;sample_adc();assert(state.adc.errors==5);adc_start_ok=true;
    sample_adc();assert(state.adc.state==SRNOS_MONITOR_ADC_READY);
    adc_stop_ok=false;sample_adc();assert(quarantined && state.adc.state==SRNOS_MONITOR_ADC_QUARANTINED);
    unsigned old=attempts;sample_adc();assert(old==attempts);
    sample_tasks();assert(delays==15 && state.task_count==3);
    clock_us+=1000000;idle.runtime+=800000;self.runtime+=500;
    sample_tasks();assert(delays==30 && state.idle_ppm>750000 && state.monitor_cpu_ppm<1000);
    state.updated_us=clock_us;state.sequence=1;SRNOS_MonitorPublish(&state);
    SRNOS_SystemMonitorSnapshot s;assert(SRNOS_SystemMonitorGetSnapshot(&s));
    fake_ipsr=1;assert(!SRNOS_SystemMonitorGetSnapshot(&s));fake_ipsr=0;
    clock_us+=4000000;assert(SRNOS_SystemMonitorGetSnapshot(&s) && s.flags&SRNOS_MONITOR_STALE && !s.adc.valid_fields);
    SRNOS_MonitorTask t;assert(SRNOS_SystemMonitorGetTask(1,&t) && t.flags&SRNOS_MONITOR_TASK_STALE && !(t.flags&2));
    assert(!SRNOS_SystemMonitorGetTask(0,&t) && !SRNOS_SystemMonitorGetTask(65,&t));
}
static void telemetry_tests(const char *directory)
{
    assert(SRNOS_SystemMonitorTelemetryRegister(NULL,NULL)==SRNOS_HOST_LINK_OK);
    assert(SRNOS_SystemMonitorTelemetryRegister(NULL,NULL)!=SRNOS_HOST_LINK_OK);
    configs[1].on_config(configs[1].user,true,1000000);
    configs[1].poll(configs[1].user,0);assert(last_id==2 && last_count==1 && last_size==128);
    assert(last_payload[0]==2);
    char path[1024];snprintf(path,sizeof(path),"%s/adc.bin",directory);FILE *f=fopen(path,"wb");assert(f);fwrite(last_payload,1,last_size,f);fclose(f);
    unsigned before=publications;
    configs[1].poll(configs[1].user,999);assert(publications==before);
    publish_result=SRNOS_HOST_LINK_ERR_FULL;configs[1].poll(configs[1].user,1000);assert(publications==before+1);
    configs[1].poll(configs[1].user,1001);assert(publications==before+1);
    configs[1].on_config(configs[1].user,false,0);configs[1].poll(configs[1].user,5000);assert(publications==before+1);
    configs[1].on_config(configs[1].user,true,1000000);publish_result=SRNOS_HOST_LINK_OK;
    configs[1].poll(configs[1].user,5001);assert(publications==before+2);
    configs[0].on_config(configs[0].user,true,1000000);configs[0].poll(configs[0].user,0);
    assert(last_id==1 && last_count==3 && last_size==336);
    snprintf(path,sizeof(path),"%s/tasks.bin",directory);f=fopen(path,"wb");assert(f);fwrite(last_payload,1,last_size,f);fclose(f);
    /* New cycle wire preserves signed jitter and its own sampling window. */
    SRNOS_MonitorTask cycle_task={.slot=1,.generation=7,.flags=SRNOS_MONITOR_TASK_ACTIVE};
    strcpy(cycle_task.name,"SRNOS_INS");
    cycle_task.sampled_us=clock_us;
    cycle_task.cycle=(SRNOS_MonitorCycle){.sampled_us=clock_us-50,.window_us=1000017,
      .flags=15,.period_ms=1,.completed=998,.jitter_samples=998,.jitter_min_us=-17,
      .jitter_max_us=22,.execution_min_us=1,.execution_mean_us=2,.execution_max_us=3};
    state.updated_us=clock_us;SRNOS_MonitorPublish(&state);SRNOS_MonitorPublishTask(&cycle_task);
    configs[2].on_config(configs[2].user,true,1000000);configs[2].poll(configs[2].user,0);
    assert(last_id==3 && last_count==3 && last_size==336 && last_payload[0]==1);
    snprintf(path,sizeof(path),"%s/cycles.bin",directory);f=fopen(path,"wb");assert(f);fwrite(last_payload,1,last_size,f);fclose(f);
    before=publications;configs[2].poll(configs[2].user,999);assert(publications==before);
    publish_result=SRNOS_HOST_LINK_ERR_FULL;configs[2].poll(configs[2].user,1000);assert(publications==before+1);
    configs[2].poll(configs[2].user,1001);assert(publications==before+1);
    configs[2].on_config(configs[2].user,false,0);configs[2].poll(configs[2].user,2000);assert(publications==before+1);
    /* All 64 slots appear exactly once over 16 four-record reports. */
    for(unsigned n=1;n<=64;n++) {cycle_task.slot=(uint8_t)n;SRNOS_MonitorPublishTask(&cycle_task);}
    configs[2].on_config(configs[2].user,true,1000000);publish_result=SRNOS_HOST_LINK_OK;
    uint64_t seen=0;
    for(unsigned n=0;n<16;n++) {
      configs[2].poll(configs[2].user,3000+n*1000);
      assert(last_count==4 && last_size==448);
      for(unsigned j=0;j<4;j++){unsigned slot=last_payload[j*112+1];assert(slot && slot<=64);uint64_t bit=UINT64_C(1)<<(slot-1);assert(!(seen&bit));seen|=bit;}
    }
    assert(seen==UINT64_MAX);
    uint8_t b[128];assert(!SRNOS_MonitorEncodeCycle(&state,&cycle_task,b,111));
    assert(!SRNOS_MonitorEncodeSnapshot(&state,b,127));
    SRNOS_MonitorTask t={0};assert(!SRNOS_MonitorEncodeTask(&state,&t,b,111));
}
int main(int argc,char **argv)
{
    assert(argc==2);conversion_tests();trace_tests();service_tests();telemetry_tests(argv[1]);
    puts("PASS: conversion, ADC lifecycle faults, late processing, slot generations, wrap, snapshots, bounded batches, telemetry pacing/backpressure");
}
