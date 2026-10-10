#include "SirinEmbedOS/IO/CAN/Router/CAN.h"
#include "can_router_core_internal.h"
#include <cassert>
#include <cstdio>
#include <cstring>

/* Virtual time and synchronous hardware model verify delivery/accounting.
 * This is deliberately not an MCU timing or physical CAN throughput test. */
unsigned SRNOS_TestCANCriticalEntries;
static uint32_t now_ms;
static uint32_t received[5], sent[5], notified[5];
static uint8_t free_slots[5];
static uint32_t frame_sequence(const SRNOS_CANFrame *f)
{
    uint32_t result;
    std::memcpy(&result, f->data, sizeof(result));
    return result;
}
extern "C" uint32_t HAL_GetTick(void) { return now_ms; }
static uint32_t now() { return now_ms; }
static uint8_t tx_free(SRNOS_CANChannel ch) { return free_slots[ch]; }
static bool push(SRNOS_CANChannel ch, const SRNOS_CANFrame *f, SRNOS_CANRouterTxClass kind)
{
    assert(kind == SRNOS_CAN_ROUTER_TX_CLASS_URGENT);
    if (!free_slots[ch]) return false;
    assert(frame_sequence(f) == sent[ch]);
    assert(f->id == 0x300u + ch && f->dlc == 4u);
    for (unsigned i=4; i<8; ++i) assert(f->data[i] == 0u);
    --free_slots[ch]; ++sent[ch]; return true;
}
static void signal(SRNOS_CANChannel ch) { ++notified[ch]; }
static const SRNOS_CANRouterBackendOps backend = {nullptr,tx_free,push,signal,now,nullptr};
static void receive(SRNOS_CANChannel ch, const SRNOS_CANFrame *f, void *)
{
    assert(frame_sequence(f) == received[ch]);
    assert(f->id == 0x100u + received[ch]%32u && f->dlc == 4u);
    for (unsigned i=4; i<8; ++i) assert(f->data[i] == 0u);
    ++received[ch];
}
int main()
{
    for (SRNOS_CANChannel ch=1; ch<=4; ++ch) {
        SRNOS_CANRouterCoreSetBackend(ch,&backend);
        for (unsigned id=0; id<32; ++id) {
            SRNOS_CANRxFilter filter{}; filter.id=0x100u+id;
            assert(SRNOS_CANRouterSubscribeRaw(ch,filter,receive,nullptr)==0);
        }
    }
    SRNOS_CAN_FreezeConfiguration();
    for (now_ms=0; now_ms<1000; ++now_ms) {
        for (SRNOS_CANChannel ch=1; ch<=4; ++ch) {
            free_slots[ch]=3;
            for (unsigned i=0; i<3; ++i) {
                const uint32_t sequence=now_ms*3+i;
                SRNOS_CANFrame f{}; f.id=0x300u+ch; f.dlc=4;
                std::memset(f.data,0xa5,sizeof(f.data));
                std::memcpy(f.data,&sequence,sizeof(sequence));
                assert(SRNOS_CANRouterPostOneshot(ch,&f,1,20)==0);
                f.id=0x100u+sequence%32u;
                const unsigned locks=SRNOS_TestCANCriticalEntries;
                SRNOS_CANRouterCoreOnRxFrame(ch,&f,now_ms);
                assert(SRNOS_TestCANCriticalEntries==locks);
            }
            SRNOS_CANRouterCoreDrainTx(ch,now_ms);
            assert(free_slots[ch]==0);
            for (unsigned i=0; i<3; ++i) SRNOS_CANRouterCoreNoteTxComplete(ch);
            assert(SRNOS_CANRouterCoreNextTxWait(ch,now_ms)==UINT32_MAX);
        }
    }
    for (SRNOS_CANChannel ch=1; ch<=4; ++ch) {
        SRNOS_CANRouterStats stats{};
        assert(SRNOS_CANRouterGetStats(ch,&stats));
        assert(received[ch]==3000 && sent[ch]==3000 && notified[ch]==3000);
        assert(stats.rx_pops==3000 && stats.rx_routed_raw==3000 && stats.rx_dropped==0);
        assert(stats.tx_urgent_accepted==3000 && stats.tx_complete_events==3000);
        assert(stats.tx_oneshot_depth==0 && stats.tx_oneshot_expired==0);
        assert(stats.rx_route_lookup_steps<=3000u*7u);
    }
    std::puts("four CAN channels: 3000 RX + 3000 TX each, frozen dispatch, sequence/accounting PASS (virtual time only)");
}
