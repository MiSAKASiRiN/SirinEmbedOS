#include "SirinEmbedOS/Kernel/Startup.h"
#include "SirinEmbedOS/Kernel/Thread.h"
#include "SirinEmbedOS/IO/CAN/FD/CANFD.h"
#include "SirinEmbedOS/Services/SystemMonitor.h"
#include "SirinEmbedOS/Services/INS.h"
#include "adapter.h"
#include <atomic>
#include <cstring>

namespace {
constexpr unsigned buses[]={1,3}, ids[]={0x601,0x603};
constexpr unsigned functional_std[]={0x711,0x713}, functional_ext[]={0x1abc101,0x1abc103};
constexpr unsigned lengths[]={0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64};
struct Load { unsigned batches[2], length, seconds; };
#if SRNOS_TEST_FD
constexpr Load loads[]={
    {{0,0},8,3},{{1,0},8,2},{{0,1},8,2},{{3,3},8,10},
    {{1,1},64,5},{{2,2},64,5},
    {{3,3},64,SRNOS_TEST_DATA_RATE==8000000?60u:10u},
    {{4,4},64,5},{{1,1},8,5}
};
#else
constexpr Load loads[]={
    {{0,0},8,3},{{1,0},8,2},{{0,1},8,2},{{1,1},8,5},{{2,2},8,5},
    {{3,3},8,60},{{4,4},8,10},{{5,5},8,5},{{1,1},8,5}
};
#endif
constexpr unsigned stages=sizeof(loads)/sizeof(loads[0]);
struct Rx { std::atomic<unsigned> count{0},gaps{0},reordered{0},bad{0},next{0}; };
struct Counters { std::atomic<unsigned> ticks{0},accepted[2]{},rejected[2]{};Rx rx[2]; };
Counters counts[stages];
std::atomic<unsigned> active{stages},malformed{0};
std::atomic<unsigned> functional_expected{0},functional_received{0},functional_bad{0};
uint32_t deadline;
uint8_t pattern(unsigned sender,unsigned index,unsigned byte) {return (uint8_t)(0x5au+sender*97u+index*17u+byte*29u);}
void receive(unsigned bus,uint32_t id,unsigned length,unsigned flags,const uint8_t *data)
{
    unsigned sender=bus==3?0:1;
    const unsigned wire_flags=(SRNOS_TEST_FD && SRNOS_TEST_BRS)?SRNOS_CANFD_FRAME_FLAG_BRS:0u;
    bool ext=(flags&SRNOS_CANFD_FRAME_FLAG_EXT)!=0;
    if(id==(ext?functional_ext[sender]:functional_std[sender])) {
        unsigned index=functional_expected.load(std::memory_order_acquire);
        bool good=index<16 && length==lengths[index] && flags==(wire_flags|(ext?SRNOS_CANFD_FRAME_FLAG_EXT:0u));
        for(unsigned i=0;i<length;++i)good=good && data[i]==pattern(sender,index,i);
        if(!good)++functional_bad;
        ++functional_received;return;
    }
    if(id!=ids[sender] || flags!=wire_flags || length<8 || data[0]>=stages) {++malformed;return;}
    unsigned stage=data[0];Rx &r=counts[stage].rx[sender];
    uint32_t seq=(uint32_t)data[2]|(uint32_t)data[3]<<8|(uint32_t)data[4]<<16|(uint32_t)data[5]<<24;
    uint8_t check=0;for(unsigned i=0;i<6;++i)check^=data[i];
    bool good=length==loads[stage].length && data[1]==buses[sender] && data[6]==check && data[7]==(uint8_t)~check;
    for(unsigned i=8;i<length;++i)good=good && data[i]==pattern(sender,seq,i);
    if(!good){++r.bad;return;}
    unsigned expected=r.next.load(std::memory_order_relaxed);
    if(seq<expected)++r.reordered;else{r.gaps.fetch_add(seq-expected);r.next.store(seq+1);}
    ++r.count;
}
void classic_rx(SRNOS_CANChannel bus,const SRNOS_CANFrame *f,void *) {receive(bus,f->id,f->dlc,f->flags,f->data);}
void fd_rx(SRNOS_CANChannel bus,const SRNOS_CANFDFrame *f,void *) {receive(bus,f->id,f->length,f->flags,f->data);}
int post(unsigned sender,uint32_t id,unsigned length,bool ext,const uint8_t *data)
{
#if SRNOS_TEST_FD
    SRNOS_CANFDFrame f{};f.id=id;f.length=length;
    f.flags=(ext?SRNOS_CANFD_FRAME_FLAG_EXT:0u)|(SRNOS_TEST_BRS?SRNOS_CANFD_FRAME_FLAG_BRS:0u);
    std::memcpy(f.data,data,length);
    return SRNOS_CANFDPostOneshot(buses[sender],&f,4,100);
#else
    SRNOS_CANFrame f{};f.id=id;f.dlc=length;f.flags=ext?SRNOS_CAN_FRAME_FLAG_EXT:0u;
    std::memcpy(f.data,data,length);
    return SRNOS_CANRouterPostOneshot(buses[sender],&f,4,100);
#endif
}
SRNOS_StepResult produce(void *,const SRNOS_ThreadWake *)
{
    unsigned stage=active.load(std::memory_order_acquire);
    if(stage>=stages || (int32_t)(SRNOS_TimeMs()-deadline)>=0)return SRNOS_STEP_CONTINUE;
    Counters &c=counts[stage];++c.ticks;
    for(unsigned sender=0;sender<2;++sender)for(unsigned n=0;n<loads[stage].batches[sender];++n) {
        unsigned seq=c.accepted[sender].load(std::memory_order_relaxed);
        uint8_t data[64]={};data[0]=stage;data[1]=buses[sender];
        for(unsigned i=0;i<4;++i)data[2+i]=(uint8_t)(seq>>(8*i));
        for(unsigned i=0;i<6;++i)data[6]^=data[i];data[7]=(uint8_t)~data[6];
        for(unsigned i=8;i<loads[stage].length;++i)data[i]=pattern(sender,seq,i);
        if(post(sender,ids[sender],loads[stage].length,false,data)==0)++c.accepted[sender];else ++c.rejected[sender];
    }
    return SRNOS_STEP_CONTINUE;
}
struct Health {
    uint32_t ins_cycles,ins_missing,ins_missed,ins_publications,ins_phase,ins_flags;
    uint32_t samples[4],errors[4],overruns[4];
};
void health(Health &out)
{
    SRNOS_INSStatus ins{};SRNOS_INSGetStatus(&ins);
    out.ins_cycles=ins.cycles;out.ins_missing=ins.missing_cycles;out.ins_missed=ins.missed_deadlines;
    out.ins_publications=ins.publications;out.ins_phase=ins.phase;out.ins_flags=ins.flags;
    for(unsigned i=0;i<4;++i){SRNOS_SensorStatus s{};SRNOS_SensorGetStatus((SRNOS_SensorId)i,&s);
        out.samples[i]=s.samples;out.errors[i]=s.errors;out.overruns[i]=s.overruns;}
}
}
struct SRNOS_TestCANStage {
    uint32_t requested_fps[2],length,seconds,producer_ticks;
    uint64_t elapsed_us;
    uint32_t accepted[2],rejected[2],received_active[2],received[2],gaps[2],reordered[2],bad[2];
    uint32_t cpu_ppm,monitor_ppm,critical_us;
    SRNOS_CANRouterStats classic_before[2],classic_after[2];
    SRNOS_CANFDStats fd_before[2],fd_after[2];
    SRNOS_TestCANHardware hardware_before[2],hardware_after[2];
    Health before,after;
};
struct SRNOS_TestCANResult {
    uint32_t phase,completed,failure,malformed,warmup_ms,fd,data_rate,brs;
    int32_t subscribe_result,create_result,delete_result;
    uint32_t functional_expected,functional_received,functional_bad;
    SRNOS_SystemStatus startup;
    SRNOS_CANControllerConfig config[2];
    SRNOS_TestCANStage stages[stages];
    uint32_t stack_count;
    SRNOS_TestCANStack stacks[48];
    SRNOS_KernelResources resources;
};
SRNOS_TestCANResult SRNOS_TestCAN;

void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *c)
{
    c->application_stack_bytes=4096;
    SRNOS_TestCAN.fd=SRNOS_TEST_FD;SRNOS_TestCAN.data_rate=SRNOS_TEST_DATA_RATE;SRNOS_TestCAN.brs=SRNOS_TEST_BRS;
    for(unsigned sender=0;sender<2;++sender) {
        unsigned bus=buses[1-sender];
#if SRNOS_TEST_FD
        c->can[bus-1]={SRNOS_CAN_MODE_FD,1000000u,SRNOS_TEST_DATA_RATE,SRNOS_TEST_BRS!=0};
#endif
        for(unsigned kind=0;kind<3;++kind) {
            SRNOS_CANRxFilter filter{kind==0?ids[sender]:kind==1?functional_std[sender]:functional_ext[sender],
                (uint8_t)(kind==2?SRNOS_CAN_FRAME_FLAG_EXT:0u)};
#if SRNOS_TEST_FD
            int rc=SRNOS_CANFDSubscribeRaw(bus,filter,fd_rx,nullptr);
#else
            int rc=SRNOS_CANRouterSubscribeRaw(bus,filter,classic_rx,nullptr);
#endif
            if(rc)SRNOS_TestCAN.subscribe_result=rc;
        }
    }
}
extern "C" __attribute__((noinline)) void SRNOS_TestCANDone(void) {__asm volatile("nop" ::: "memory");}
void SRNOS_ApplicationMain()
{
    auto finish=[](){
        SRNOS_TestCAN.malformed=malformed.load();
        SRNOS_TestCAN.functional_received=functional_received.load();
        SRNOS_TestCAN.functional_bad=functional_bad.load();
        SRNOS_TestCAN.stack_count=SRNOS_TestCANStacks(SRNOS_TestCAN.stacks,48);
        (void)SRNOS_KernelGetResources(&SRNOS_TestCAN.resources);
        SRNOS_TestCAN.phase=3;SRNOS_TestCANDone();
    };
    SRNOS_SystemGetStatus(&SRNOS_TestCAN.startup);
    if(SRNOS_TestCAN.subscribe_result || !SRNOS_TestCAN.startup.native_can_ready){SRNOS_TestCAN.failure=1;finish();return;}
    for(unsigned i=0;i<2;++i)(void)SRNOS_CANControllerGetConfig(buses[i],&SRNOS_TestCAN.config[i]);
    SRNOS_TestCAN.phase=1;uint32_t start=SRNOS_TimeMs();
    for(;;){SRNOS_INSStatus s{};SRNOS_INSGetStatus(&s);if(s.flags&SRNOS_INS_VALID)break;
        if(SRNOS_TimeMs()-start>=120000){SRNOS_TestCAN.failure=4;finish();return;}SRNOS_Delay(1000);}
    SRNOS_TestCAN.warmup_ms=SRNOS_TimeMs()-start;SRNOS_Delay(3000);
    // One outstanding functional frame at a time: both directions, STD/EXT,
    // all exact legal lengths, full byte validation including short/zero FD.
    for(unsigned sender=0;sender<2;++sender)for(unsigned ext=0;ext<2;++ext)
      for(unsigned index=0;index<(SRNOS_TEST_FD?16u:9u);++index) {
        uint8_t data[64];for(unsigned i=0;i<64;++i)data[i]=pattern(sender,index,i);
        functional_expected.store(index,std::memory_order_release);
        ++SRNOS_TestCAN.functional_expected;
        if(post(sender,ext?functional_ext[sender]:functional_std[sender],lengths[index],ext,data)!=0){SRNOS_TestCAN.failure=5;finish();return;}
        uint32_t until=SRNOS_TimeMs()+200;
        while(functional_received.load()!=SRNOS_TestCAN.functional_expected && (int32_t)(SRNOS_TimeMs()-until)<0)SRNOS_Delay(1);
        if(functional_received.load()!=SRNOS_TestCAN.functional_expected || functional_bad.load()){SRNOS_TestCAN.failure=6;finish();return;}
      }
    SRNOS_Delay(500);
    SRNOS_ThreadConfig desc{};desc.name="CANLoad";desc.step=produce;desc.stack_bytes=2048;
    desc.priority=24;desc.mode=SRNOS_TASK_PERIODIC;desc.period_ms=1;
    SRNOS_Thread producer;
    SRNOS_TestCAN.create_result=SRNOS_ThreadCreate(&desc,&producer);
    if(SRNOS_TestCAN.create_result!=SRNOS_OK){SRNOS_TestCAN.failure=2;finish();return;}
    for(unsigned stage=0;stage<stages;++stage) {
        auto &r=SRNOS_TestCAN.stages[stage];r.seconds=loads[stage].seconds;r.length=loads[stage].length;
        SRNOS_TestCAN.phase=2;
        for(unsigned i=0;i<2;++i) {
            r.requested_fps[i]=loads[stage].batches[i]*1000;
            SRNOS_CANRouterGetStats(buses[i],&r.classic_before[i]);SRNOS_CANFDGetStats(buses[i],&r.fd_before[i]);
            SRNOS_TestCANHardwareRead(buses[i],&r.hardware_before[i]);
        }
        health(r.before);uint64_t c0,i0,c1,i1;SRNOS_TestIdleRead(&c0,&i0);
        uint64_t start_us=SRNOS_TimeUs();uint32_t stop=SRNOS_TimeMs()+r.seconds*1000;
        deadline=stop;active.store(stage,std::memory_order_release);
        for(unsigned second=0;second<r.seconds;++second) {
            SRNOS_DelayUntil(stop-(r.seconds-second-1)*1000);
            SRNOS_SystemMonitorSnapshot s{};SRNOS_SystemMonitorGetSnapshot(&s);
            r.monitor_ppm+=s.monitor_cpu_ppm;if(s.max_critical_us>r.critical_us)r.critical_us=s.max_critical_us;
        }
        active.store(stages,std::memory_order_release);
        r.elapsed_us=SRNOS_TimeUs()-start_us;SRNOS_TestIdleRead(&c1,&i1);health(r.after);
        r.cpu_ppm=1000000-(uint32_t)((i1-i0)*1000000/(c1-c0));r.monitor_ppm/=r.seconds;
        for(unsigned i=0;i<2;++i)r.received_active[i]=counts[stage].rx[i].count.load();
        SRNOS_Delay(500);
        r.producer_ticks=counts[stage].ticks.load();
        for(unsigned i=0;i<2;++i) {
            auto &rx=counts[stage].rx[i];r.accepted[i]=counts[stage].accepted[i].load();r.rejected[i]=counts[stage].rejected[i].load();
            r.received[i]=rx.count.load();r.gaps[i]=rx.gaps.load();r.reordered[i]=rx.reordered.load();r.bad[i]=rx.bad.load();
            SRNOS_CANRouterGetStats(buses[i],&r.classic_after[i]);SRNOS_CANFDGetStats(buses[i],&r.fd_after[i]);
            SRNOS_TestCANHardwareRead(buses[i],&r.hardware_after[i]);
            if(r.hardware_after[i].bus_off || r.hardware_after[i].tx_pending || (r.accepted[i]&&!r.received[i]))SRNOS_TestCAN.failure=3;
        }
        SRNOS_TestCAN.completed=stage+1;if(SRNOS_TestCAN.failure)break;
    }
    // Capture the producer too, after draining all traffic and before Delete.
    SRNOS_TestCAN.stack_count=SRNOS_TestCANStacks(SRNOS_TestCAN.stacks,48);
    SRNOS_TestCAN.delete_result=SRNOS_ThreadDelete(producer,1000);
    SRNOS_TestCAN.malformed=malformed.load();SRNOS_TestCAN.functional_received=functional_received.load();
    SRNOS_TestCAN.functional_bad=functional_bad.load();(void)SRNOS_KernelGetResources(&SRNOS_TestCAN.resources);
    SRNOS_TestCAN.phase=3;SRNOS_TestCANDone();
}
