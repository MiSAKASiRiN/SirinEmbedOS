#include "SirinEmbedOS/Kernel/Startup.h"
#include "SirinEmbedOS/Kernel/Sync.h"
#include "SirinEmbedOS/Services/SystemMonitor.h"
#include "SirinEmbedOS/Services/INS.h"
#include "SirinEmbedOS/IO/CAN/Router/CAN.h"
#include "kernel_adapter.h"
#include <cstring>
#include <cerrno>
struct BoardTest {
 uint32_t stage,passed,failed,last_failure,iterations;
 bool stats,monitor;
 HookBenchmark hook;
 SRNOS_KernelResources resources_before,resources_after;
 uint32_t busy_ppm[30],monitor_ppm[30],critical_us[30],samples;
 SRNOS_INSStatus ins_before,ins_after;
 SRNOS_SensorStatus sensors_before[4],sensors_after[4];
 SRNOS_SystemMonitorSnapshot snapshot;
 SRNOS_CANRouterStats SRNOS_CANBefore[4],SRNOS_CANAfter[4];
};
volatile BoardTest board_test;
static void check(bool ok,unsigned line) {if(ok)++board_test.passed;else{++board_test.failed;board_test.last_failure=line;}}
#define CHECK(x) check((x),__LINE__)
static thread_local unsigned tls_seed=0x12345678;
static thread_local unsigned tls_zero;
struct Context {volatile unsigned init,steps,clean,entered;unsigned behavior;SRNOS_Queue queue;SRNOS_Mutex mutex;};
static SRNOS_Result init(void *p) {
 auto *c=(Context*)p;++c->init;
 CHECK(tls_seed==0x12345678 && tls_zero==0);tls_zero=1;errno=9;
 if(c->behavior==1)return SRNOS_INVALID;
 if(c->behavior==2){c->entered=1;CHECK(SRNOS_Delay(SRNOS_WAIT_FOREVER)==SRNOS_STOP_REQUESTED);}
 return SRNOS_OK;
}
static void clean(void *p){auto*c=(Context*)p;CHECK(tls_zero==1 && errno==9);++c->clean;}
static SRNOS_StepResult step(void *p,const SRNOS_ThreadWake *w) {
 auto*c=(Context*)p;++c->steps;
 if(c->behavior==3){CHECK(w->flags==1);return SRNOS_STEP_STOP;}
 if(c->behavior==4){c->entered=1;unsigned v=0;CHECK(SRNOS_QueueReceive(c->queue,&v,100)==SRNOS_OK && v==123);}
 if(c->behavior==5){CHECK(SRNOS_MutexLock(c->mutex,0)==SRNOS_OK);c->entered=1;
  CHECK(SRNOS_Delay(SRNOS_WAIT_FOREVER)==SRNOS_STOP_REQUESTED);CHECK(SRNOS_MutexUnlock(c->mutex)==SRNOS_OK);}
 return SRNOS_STEP_STOP;
}
static SRNOS_ThreadConfig descriptor(Context *c) {
 SRNOS_ThreadConfig d{};d.name="LifeTest";d.init=init;d.step=step;d.cleanup=clean;d.argument=c;
 d.stack_bytes=2048;d.priority=25;d.mode=SRNOS_TASK_ONCE;return d;
}
static void entered(Context *c) {uint32_t t=SRNOS_TimeMs();while(!c->entered && SRNOS_TimeMs()-t<100)SRNOS_Delay(1);CHECK(c->entered);}
static void lifecycle() {
 SRNOS_KernelGetResources((SRNOS_KernelResources*)&board_test.resources_before);
 Context c{};SRNOS_Thread h=0,old=0;SRNOS_ThreadInfo info;
 for(unsigned n=0;n<400;n++) {
  c={};auto d=descriptor(&c);
  if(SRNOS_ThreadCreate(&d,&h)!=SRNOS_OK){CHECK(false);break;}
  CHECK(SRNOS_ThreadJoin(h,100)==SRNOS_OK);
  CHECK(c.init==1 && c.steps==1 && c.clean==1);
  CHECK(SRNOS_ThreadDelete(h,0)==SRNOS_OK);
  CHECK(SRNOS_ThreadGetInfo(h,&info)==SRNOS_STALE_HANDLE);
  if(old)CHECK(old!=h);old=h;++board_test.iterations;SRNOS_Delay(1);
 }
 c={};c.behavior=1;auto d=descriptor(&c);CHECK(SRNOS_ThreadCreate(&d,&h)==SRNOS_OK);
 CHECK(SRNOS_ThreadJoin(h,100)==SRNOS_OK);CHECK(!c.steps && c.clean==1);
 CHECK(SRNOS_ThreadGetInfo(h,&info)==SRNOS_OK && info.state==SRNOS_THREAD_FAILED);CHECK(SRNOS_ThreadDelete(h,0)==SRNOS_OK);
 c={};c.behavior=2;d=descriptor(&c);CHECK(SRNOS_ThreadCreate(&d,&h)==SRNOS_OK);entered(&c);
 CHECK(SRNOS_ThreadDelete(h,100)==SRNOS_OK && !c.steps && c.clean==1);
 c={};c.behavior=3;d=descriptor(&c);d.mode=SRNOS_TASK_EVENT;d.event_mask=1;d.event_timeout_ms=SRNOS_WAIT_FOREVER;
 CHECK(SRNOS_ThreadCreate(&d,&h)==SRNOS_OK);CHECK(SRNOS_ThreadNotify(h,1)==SRNOS_OK);
 CHECK(SRNOS_ThreadJoin(h,100)==SRNOS_OK && c.steps==1 && c.clean==1);CHECK(SRNOS_ThreadDelete(h,0)==SRNOS_OK);
 c={};c.behavior=4;CHECK(SRNOS_QueueCreate(2,sizeof(unsigned),&c.queue)==SRNOS_OK);d=descriptor(&c);
 CHECK(SRNOS_ThreadCreate(&d,&h)==SRNOS_OK);entered(&c);CHECK(SRNOS_QueueDelete(c.queue)==SRNOS_BUSY);
 CHECK(SRNOS_ThreadDelete(h,1)==SRNOS_TIMEOUT);unsigned value=123;CHECK(SRNOS_QueueSend(c.queue,&value,0)==SRNOS_OK);
 CHECK(SRNOS_ThreadDelete(h,100)==SRNOS_OK && c.clean==1);CHECK(SRNOS_QueueDelete(c.queue)==SRNOS_OK);
 c={};c.behavior=5;CHECK(SRNOS_MutexCreate(false,&c.mutex)==SRNOS_OK);d=descriptor(&c);CHECK(SRNOS_ThreadCreate(&d,&h)==SRNOS_OK);
 entered(&c);CHECK(SRNOS_MutexDelete(c.mutex)==SRNOS_BUSY);CHECK(SRNOS_ThreadDelete(h,100)==SRNOS_OK);CHECK(SRNOS_MutexDelete(c.mutex)==SRNOS_OK);
 SRNOS_Queue large;CHECK(SRNOS_QueueCreate(30000,1,&large)==SRNOS_OK);d.stack_bytes=4096;
 CHECK(SRNOS_ThreadCreate(&d,&h)==SRNOS_RESOURCE);CHECK(SRNOS_QueueDelete(large)==SRNOS_OK);
 SRNOS_KernelGetResources((SRNOS_KernelResources*)&board_test.resources_after);
 CHECK(board_test.resources_after.used_bytes==board_test.resources_before.used_bytes);
}
static volatile uint32_t work_value;
static SRNOS_StepResult work(void*,const SRNOS_ThreadWake*) {
 uint64_t until=SRNOS_TimeUs()+20;while(SRNOS_TimeUs()<until)++work_value;return SRNOS_STEP_CONTINUE;
}
static void capture(bool after) {
 SRNOS_INSGetStatus(after?(SRNOS_INSStatus*)&board_test.ins_after:(SRNOS_INSStatus*)&board_test.ins_before);
 for(unsigned n=0;n<4;n++){
  SRNOS_SensorGetStatus((SRNOS_SensorId)n,after?(SRNOS_SensorStatus*)&board_test.sensors_after[n]:(SRNOS_SensorStatus*)&board_test.sensors_before[n]);
  SRNOS_CANRouterStats s{};if(SRNOS_CANRouterGetStats((SRNOS_CANChannel)(n+1),&s))std::memcpy(after?(void*)&board_test.SRNOS_CANAfter[n]:(void*)&board_test.SRNOS_CANBefore[n],&s,sizeof(s));
 }
}
void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *c) {
 c->cycle_statistics_enabled=BOARD_STATS;c->system_monitor_enabled=BOARD_MONITOR;
 c->sensors.heater_enabled=false; // Same thermal conditions in all comparison builds.
 c->application_stack_bytes=4096;
}
void SRNOS_ApplicationMain() {
 board_test.stats=BOARD_STATS;board_test.monitor=BOARD_MONITOR;board_test.stage=1;
 board_hook_benchmark((HookBenchmark*)&board_test.hook);lifecycle();
 SRNOS_Thread task;SRNOS_ThreadConfig d{};d.name="ControlLoad";d.step=work;d.stack_bytes=2048;
 d.priority=24;d.mode=SRNOS_TASK_PERIODIC;d.period_ms=1;CHECK(SRNOS_ThreadCreate(&d,&task)==SRNOS_OK);
 board_test.stage=2;SRNOS_Delay(5000);capture(false);
 uint64_t time0,idle0;board_idle_counters(&time0,&idle0);
 for(unsigned i=0;i<30;i++){
  SRNOS_Delay(1000);uint64_t time1,idle1;board_idle_counters(&time1,&idle1);
  board_test.busy_ppm[i]=1000000-(uint32_t)((idle1-idle0)*1000000/(time1-time0));time0=time1;idle0=idle1;
  SRNOS_SystemMonitorGetSnapshot((SRNOS_SystemMonitorSnapshot*)&board_test.snapshot);
  board_test.monitor_ppm[i]=board_test.snapshot.monitor_cpu_ppm;board_test.critical_us[i]=board_test.snapshot.max_critical_us;board_test.samples=i+1;
 }
 capture(true);board_test.stage=3;
}
