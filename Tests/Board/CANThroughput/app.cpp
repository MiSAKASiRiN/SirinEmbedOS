#include "SirinEmbedOS/Kernel/Startup.h"
#include "SirinEmbedOS/Kernel/Thread.h"
#include "SirinEmbedOS/IO/CAN/Router/CAN.h"
#include "SirinEmbedOS/Services/SystemMonitor.h"
#include "SirinEmbedOS/Services/INS.h"
#include "adapter.h"
#include <atomic>

namespace {
struct Load { unsigned batches[2], seconds; };
constexpr Load loads[] = {
    {{0,0},5}, {{1,0},2}, {{0,1},2}, {{1,1},10}, {{2,2},10},
    {{3,3},10}, {{4,4},10}, {{5,5},10}, {{1,1},5}
};
constexpr unsigned stages = sizeof(loads)/sizeof(loads[0]);
constexpr unsigned buses[] = {1,3};
constexpr unsigned ids[] = {0x601,0x603};
struct Rx {
    std::atomic<unsigned> count{0}, gaps{0}, reordered{0}, bad{0}, next{0};
};
struct Counters {
    std::atomic<unsigned> ticks{0}, accepted[2]{}, rejected[2]{};
    Rx rx[2]; // Indexed by sender: CAN1 -> CAN3, CAN3 -> CAN1.
};
Counters counts[stages];
std::atomic<unsigned> active{stages};
uint32_t deadline;
std::atomic<unsigned> malformed{0};
void receive(SRNOS_CANChannel bus, const SRNOS_CANFrame *f, void *)
{
    unsigned sender = bus == 3 ? 0 : 1;
    if (f->dlc != 8 || f->flags || f->id != ids[sender] || f->data[0] >= stages) {
        ++malformed; return;
    }
    Rx &r = counts[f->data[0]].rx[sender];
    uint8_t check = 0;
    for (unsigned i=0;i<6;++i) check ^= f->data[i];
    if (f->data[1] != buses[sender] || f->data[6] != check ||
        f->data[7] != (uint8_t)~check) { ++r.bad; return; }
    uint32_t seq = (uint32_t)f->data[2] | (uint32_t)f->data[3]<<8 |
                   (uint32_t)f->data[4]<<16 | (uint32_t)f->data[5]<<24;
    unsigned expected = r.next.load(std::memory_order_relaxed);
    if (seq < expected) ++r.reordered;
    else { r.gaps.fetch_add(seq-expected); r.next.store(seq+1); }
    ++r.count;
}
SRNOS_StepResult produce(void *, const SRNOS_ThreadWake *)
{
    unsigned stage = active.load(std::memory_order_acquire);
    if (stage >= stages || (int32_t)(SRNOS_TimeMs()-deadline) >= 0)
        return SRNOS_STEP_CONTINUE;
    Counters &c = counts[stage]; ++c.ticks;
    for (unsigned sender=0;sender<2;++sender) {
        for (unsigned n=0;n<loads[stage].batches[sender];++n) {
            unsigned seq = c.accepted[sender].load(std::memory_order_relaxed);
            SRNOS_CANFrame f{};f.id=ids[sender];f.dlc=8;
            f.data[0]=stage;f.data[1]=buses[sender];
            for(unsigned i=0;i<4;++i)f.data[2+i]=(uint8_t)(seq>>(8*i));
            for(unsigned i=0;i<6;++i)f.data[6]^=f.data[i];
            f.data[7]=(uint8_t)~f.data[6];
            if(SRNOS_CANRouterPostOneshot(buses[sender],&f,4,100)==0)
                ++c.accepted[sender];
            else ++c.rejected[sender];
        }
    }
    return SRNOS_STEP_CONTINUE;
}
struct Health {
    uint32_t ins_cycles, ins_missing, ins_missed, ins_publications, ins_phase, ins_flags;
    uint32_t samples[4], errors[4], overruns[4];
};
void health(Health &out)
{
    SRNOS_INSStatus ins{};SRNOS_INSGetStatus(&ins);
    out.ins_cycles=ins.cycles;out.ins_missing=ins.missing_cycles;out.ins_missed=ins.missed_deadlines;
    out.ins_publications=ins.publications;out.ins_phase=ins.phase;out.ins_flags=ins.flags;
    for(unsigned i=0;i<4;++i) {
        SRNOS_SensorStatus s{};SRNOS_SensorGetStatus((SRNOS_SensorId)i,&s);
        out.samples[i]=s.samples;out.errors[i]=s.errors;out.overruns[i]=s.overruns;
    }
}
}
struct SRNOS_TestCANStage {
    uint32_t requested_fps[2], seconds, producer_ticks;
    uint64_t elapsed_us;
    uint32_t accepted[2], rejected[2], received_active[2], received[2];
    uint32_t gaps[2], reordered[2], bad[2], cpu_ppm, monitor_ppm, critical_us;
    SRNOS_CANRouterStats router_before[2], router_after[2];
    SRNOS_TestCANHardware hardware_before[2], hardware_after[2];
    Health before, after;
};
struct SRNOS_TestCANResult {
    uint32_t phase, completed, failure, malformed, warmup_ms;
    int32_t subscribe_result[2], create_result, delete_result;
    SRNOS_TestCANStage stages[stages];
};
SRNOS_TestCANResult SRNOS_TestCAN;

void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *c)
{
    c->application_stack_bytes=4096;
    SRNOS_TestCAN.subscribe_result[0]=SRNOS_CANRouterSubscribeRaw(3,{ids[0],0},receive,nullptr);
    SRNOS_TestCAN.subscribe_result[1]=SRNOS_CANRouterSubscribeRaw(1,{ids[1],0},receive,nullptr);
}
void SRNOS_ApplicationMain()
{
    if(SRNOS_TestCAN.subscribe_result[0] || SRNOS_TestCAN.subscribe_result[1]) {
        SRNOS_TestCAN.failure=1;SRNOS_TestCAN.phase=3;return;
    }
    SRNOS_TestCAN.phase=1;
    const uint32_t warmup_start=SRNOS_TimeMs();
    for(;;) {
        SRNOS_INSStatus s{};SRNOS_INSGetStatus(&s);
        if(s.flags&SRNOS_INS_VALID)break;
        if(SRNOS_TimeMs()-warmup_start>=120000) {
            SRNOS_TestCAN.failure=4;SRNOS_TestCAN.phase=3;return;
        }
        SRNOS_Delay(1000);
    }
    SRNOS_TestCAN.warmup_ms=SRNOS_TimeMs()-warmup_start;
    SRNOS_Delay(5000);
    SRNOS_ThreadConfig desc{};desc.name="CANLoad";desc.step=produce;
    desc.stack_bytes=2048;desc.priority=24;desc.mode=SRNOS_TASK_PERIODIC;desc.period_ms=1;
    SRNOS_Thread producer;
    SRNOS_TestCAN.create_result=SRNOS_ThreadCreate(&desc,&producer);
    if(SRNOS_TestCAN.create_result!=SRNOS_OK) {SRNOS_TestCAN.failure=2;SRNOS_TestCAN.phase=3;return;}
    for(unsigned stage=0;stage<stages;++stage) {
        auto &r=SRNOS_TestCAN.stages[stage];r.seconds=loads[stage].seconds;
        SRNOS_TestCAN.phase=2;
        for(unsigned i=0;i<2;++i) {
            r.requested_fps[i]=loads[stage].batches[i]*1000;
            SRNOS_CANRouterGetStats(buses[i],&r.router_before[i]);
            SRNOS_TestCANHardwareRead(buses[i],&r.hardware_before[i]);
        }
        health(r.before);uint64_t c0,i0,c1,i1;SRNOS_TestIdleRead(&c0,&i0);
        uint64_t start=SRNOS_TimeUs();uint32_t stop=SRNOS_TimeMs()+r.seconds*1000;
        deadline=stop;active.store(stage,std::memory_order_release);
        for(unsigned second=0;second<r.seconds;++second) {
            SRNOS_DelayUntil(stop-(r.seconds-second-1)*1000);
            SRNOS_SystemMonitorSnapshot s{};SRNOS_SystemMonitorGetSnapshot(&s);
            r.monitor_ppm+=s.monitor_cpu_ppm;
            if(s.max_critical_us>r.critical_us)r.critical_us=s.max_critical_us;
        }
        active.store(stages,std::memory_order_release);
        r.elapsed_us=SRNOS_TimeUs()-start;SRNOS_TestIdleRead(&c1,&i1);health(r.after);
        r.cpu_ppm=1000000-(uint32_t)((i1-i0)*1000000/(c1-c0));r.monitor_ppm/=r.seconds;
        for(unsigned i=0;i<2;++i)r.received_active[i]=counts[stage].rx[i].count.load();
        SRNOS_Delay(500); // > TTL, let accepted hardware/software queues drain.
        r.producer_ticks=counts[stage].ticks.load();
        for(unsigned i=0;i<2;++i) {
            auto &rx=counts[stage].rx[i];r.accepted[i]=counts[stage].accepted[i].load();
            r.rejected[i]=counts[stage].rejected[i].load();r.received[i]=rx.count.load();
            r.gaps[i]=rx.gaps.load();r.reordered[i]=rx.reordered.load();r.bad[i]=rx.bad.load();
            SRNOS_CANRouterGetStats(buses[i],&r.router_after[i]);
            SRNOS_TestCANHardwareRead(buses[i],&r.hardware_after[i]);
            if(r.hardware_after[i].bus_off || r.hardware_after[i].tx_pending ||
               (r.accepted[i] && !r.received[i]))SRNOS_TestCAN.failure=3;
        }
        SRNOS_TestCAN.completed=stage+1;
        if(SRNOS_TestCAN.failure)break;
    }
    SRNOS_TestCAN.delete_result=SRNOS_ThreadDelete(producer,1000);
    SRNOS_TestCAN.malformed=malformed.load();SRNOS_TestCAN.phase=3;
}
