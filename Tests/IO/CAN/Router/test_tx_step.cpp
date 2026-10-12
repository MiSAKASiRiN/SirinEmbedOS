#include "can_router_core_internal.h"
#include <cassert>
#include <cstdio>
#include <vector>
static uint32_t now;
static bool busy;
static std::vector<SRNOS_CANFrame> frames;
extern "C" uint32_t HAL_GetTick(void) { return now + 12u; }
extern "C" uint32_t SRNOS_TimeMs(void) { return now; }
static uint8_t free_slots(SRNOS_CANChannel){return busy?0:8;}
static bool push(SRNOS_CANChannel,const SRNOS_CANFrame *f,SRNOS_CANRouterTxClass){frames.push_back(*f);return true;}
struct Adapter {unsigned index,polls[2]{},commits[2]{},ticks{};};
static size_t poll(void *p,SRNOS_CANTxKind k,SRNOS_CANTxItem *out,size_t max,uint32_t){
    auto &a=*static_cast<Adapter*>(p);assert(max>=4);++a.polls[k];
    if(k==SRNOS_CAN_TX_KIND_URGENT && a.commits[k]>=4)return 0;
    for(unsigned i=0;i<4;++i){out[i]={};
        if(k==SRNOS_CAN_TX_KIND_PERIODIC && i==3) {
            out[i].format=SRNOS_CAN_TX_FORMAT_GROUP_I16;
            out[i].group_i16.group_id=0x200;out[i].group_i16.slot=a.index;out[i].group_i16.value=123+a.index;
        } else {out[i].format=SRNOS_CAN_TX_FORMAT_RAW_FRAME;out[i].frame.id=0x100+a.index*16+i;out[i].frame.dlc=8;}
    }
    return 4;
}
static void commit(void *p,SRNOS_CANTxKind k,const SRNOS_CANTxItem *,SRNOS_CANTxResult r){
    assert(r==SRNOS_CAN_TX_RESULT_ACCEPTED);++static_cast<Adapter*>(p)->commits[k];
}
static void tick(void *p,uint32_t){++static_cast<Adapter*>(p)->ticks;}
int main(){
    Adapter a{0},b{1};SRNOS_CANAdapterOps ops{nullptr,nullptr,nullptr,poll,commit,tick};
    assert(!SRNOS_CANRouterAttachAdapter(4,&a,&ops,10));assert(!SRNOS_CANRouterAttachAdapter(4,&b,&ops,10));
    SRNOS_CAN_FreezeConfiguration();SRNOS_CANRouterBackendOps backend{nullptr,free_slots,push,nullptr,SRNOS_TimeMs,nullptr};
    SRNOS_CANRouterCoreSetBackend(4,&backend);
    now=10;assert(SRNOS_CANRouterCoreStepTx(4,now));assert(frames.size()==1 && a.polls[SRNOS_CAN_TX_KIND_URGENT]==1);
    busy=true;assert(!SRNOS_CANRouterCoreStepTx(4,now));assert(a.polls[SRNOS_CAN_TX_KIND_URGENT]==1 && a.commits[SRNOS_CAN_TX_KIND_URGENT]==1);
    busy=false;
    for(unsigned i=0;i<20;++i){size_t before=frames.size();bool sent=SRNOS_CANRouterCoreStepTx(4,now);assert(frames.size()-before<=1);if(!sent)break;}
    assert(a.commits[0]==4 && b.commits[0]==4 && a.commits[1]==4 && b.commits[1]==4);
    assert(a.ticks==1 && b.ticks==1 && frames.size()==15);
    const auto &g=frames.back();assert(g.id==0x200 && g.data[1]==123 && g.data[3]==124);
    assert(a.polls[SRNOS_CAN_TX_KIND_PERIODIC]==1 && b.polls[SRNOS_CAN_TX_KIND_PERIODIC]==1);
    now=19;assert(!SRNOS_CANRouterCoreStepTx(4,now));assert(a.polls[SRNOS_CAN_TX_KIND_PERIODIC]==1);
    now=20;
    for(unsigned i=0;i<10 && SRNOS_CANRouterCoreStepTx(4,now);++i){}
    assert(a.commits[SRNOS_CAN_TX_KIND_PERIODIC]==8 && b.commits[SRNOS_CAN_TX_KIND_PERIODIC]==8 && a.polls[SRNOS_CAN_TX_KIND_PERIODIC]==2);
    SRNOS_CANFrame f{};f.id=0x77;f.dlc=8;
    assert(!SRNOS_CANRouterPostOneshot(4,&f,0,1));now=22;
    (void)SRNOS_CANRouterCoreStepTx(4,now);
    SRNOS_CANRouterStats stats{};assert(SRNOS_CANRouterGetStats(4,&stats));assert(stats.tx_oneshot_expired==1);
    now=30;f.id=0x78;
    for(unsigned i=0;i<40;++i)assert(!SRNOS_CANRouterPostOneshot(4,&f,0,20));
    size_t start=frames.size();
    for(unsigned i=0;i<16;++i)assert(SRNOS_CANRouterCoreStepTx(4,now));
    assert(a.commits[SRNOS_CAN_TX_KIND_PERIODIC]==8);
    for(unsigned i=0;i<7;++i)assert(SRNOS_CANRouterCoreStepTx(4,now));
    assert(a.commits[SRNOS_CAN_TX_KIND_PERIODIC]==12 && b.commits[SRNOS_CAN_TX_KIND_PERIODIC]==12);
    for(unsigned i=0;i<24;++i)assert(SRNOS_CANRouterCoreStepTx(4,now));
    assert(frames.size()-start==47 && !SRNOS_CANRouterCoreStepTx(4,now));
    puts("CAN4 TX step: one frame per call, suspended urgent batch, group coalescing, adapter cadence and TTL: PASS");
}
