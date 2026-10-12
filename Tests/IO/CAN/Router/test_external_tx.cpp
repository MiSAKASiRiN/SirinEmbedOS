#include <cassert>
#include <cstdint>
#include <cstdio>
#include "../../../../Source/IO/CAN/External/exfdcan_tx_router.cpp"

static uint32_t now_ms, query_count, fail_query_at, accepted, signals;
static bool fail_load, fail_all_queries, fifo_full, fail_init;
static SRNOS_ExternalCANStats hw_stats;
static SRNOS_ExternalCANCtx context;
extern "C" void SRNOS_CANFDCoreSetBackend(SRNOS_CANChannel,const SRNOS_CANFDBackendOps *) { assert(false); }
extern "C" uint32_t SRNOS_CANFDCoreNextTxWait(SRNOS_CANChannel,uint32_t) { assert(false);return UINT32_MAX; }
extern "C" void SRNOS_CANFDCoreDrainTx(SRNOS_CANChannel,uint32_t) { assert(false); }
extern "C" void SRNOS_CANFDCoreNoteTxComplete(SRNOS_CANChannel) { assert(false); }
extern "C" uint32_t HAL_GetTick(void) { return now_ms + 12u; }
extern "C" uint32_t SRNOS_TimeMs(void) { return now_ms; }
extern "C" int SRNOS_ExternalCANDevFromChannel(SRNOS_CANChannel ch) { return ch==4?0:-1; }
extern "C" SRNOS_CANChannel SRNOS_ExternalCANChannelFromDev(uint8_t dev) { return dev==0?4:0; }
extern "C" SRNOS_ExternalCANCtx *SRNOS_ExternalCANCtxOf(uint8_t dev) { return dev==0?&context:nullptr; }
extern "C" int SRNOS_ExternalCANHwInit(uint8_t) { return fail_init?-1:0; }
extern "C" void SRNOS_ExternalCANNotify(SRNOS_ExternalCANCtx *ctx,uint32_t bits) {
    assert(ctx==&context && bits==SRNOS_EXTERNAL_CAN_EVT_TX_ROUTER); ++signals;
}
BaseType_t xTaskNotifyWait(uint32_t,uint32_t,uint32_t *,TickType_t) { assert(false);return 0; }
extern "C" bool SRNOS_MCPFifoTransmit(SRNOS_MCPFifo *fifo,const uint8_t *,const uint8_t *,uint8_t) {
    ++query_count;fifo->status=fifo_full?0:CAN_TX_FIFO_NOT_FULL;
    if(fifo_full)return false;
    if(fail_all_queries || query_count==fail_query_at || fail_load) {fail_load=false;++fifo->errors;return false;}
    ++accepted;return true;
}

static void post(uint32_t at)
{
    now_ms=at;context={};context.stats=&hw_stats;
    query_count=fail_query_at=accepted=signals=0;fail_load=fail_all_queries=fifo_full=false;
    SRNOS_CANFrame frame{};frame.id=0x123;frame.dlc=8;
    assert(SRNOS_CANRouterCoreNextTxWait(4,now_ms)==UINT32_MAX);
    assert(SRNOS_CANRouterPostOneshot(4,&frame,0,20)==0);
    assert(signals==1);
}
static void check_retry_without_irq()
{
    SRNOS_CANRouterCoreStepTx(4,now_ms);
    assert(accepted==0 && context.tx_retry_pending);
    assert(SRNOS_CANRouterCoreNextTxWait(4,now_ms)==1);
    ++now_ms;
    assert(SRNOS_CANRouterCoreNextTxWait(4,now_ms)==0);
    SRNOS_CANRouterCoreStepTx(4,now_ms);
    assert(accepted==1 && !context.tx_retry_pending && signals==1);
    assert(SRNOS_CANRouterCoreNextTxWait(4,now_ms)==UINT32_MAX);
}
int main()
{
    SRNOS_ExternalCANBackendBind(0);
    post(100);fail_query_at=1;check_retry_without_irq();
    /* A single admission query per accepted frame, not get_free + try_push. */
    post(200);fail_query_at=2;SRNOS_CANRouterCoreStepTx(4,now_ms);
    assert(accepted==1 && query_count==1);
    post(300);fail_load=true;check_retry_without_irq();
    post(UINT32_MAX);fail_query_at=1;check_retry_without_irq();
    /* Actual FIFO full still waits for TX IRQ or TTL, with no forced tick. */
    post(400);fifo_full=true;SRNOS_CANRouterCoreStepTx(4,now_ms);
    assert(context.tx_retry_pending && SRNOS_CANRouterCoreNextTxWait(4,now_ms)==1);
    now_ms+=20;SRNOS_CANRouterCoreStepTx(4,now_ms);
    assert(!accepted && SRNOS_CANRouterCoreNextTxWait(4,now_ms)==UINT32_MAX);
    /* Persistent SPI failure cannot extend the oneshot lifetime or leave an
     * idle 1 kHz wake once the queue is empty. */
    post(500);fail_all_queries=true;
    for(unsigned i=0;i<20;i++) {
        SRNOS_CANRouterCoreStepTx(4,now_ms);
        assert(SRNOS_CANRouterCoreNextTxWait(4,now_ms)==1);++now_ms;
    }
    SRNOS_CANRouterCoreStepTx(4,now_ms);
    assert(!accepted && SRNOS_CANRouterCoreNextTxWait(4,now_ms)==UINT32_MAX);
    /* 3,000 complete router/backend admissions in one simulated second.
     * This verifies work/query counts, not physical SPI/CAN timing. */
    query_count=accepted=0;context.tx_retry_pending=false;fail_all_queries=false;
    SRNOS_CANFrame frame{};frame.id=0x123;frame.dlc=8;
    for(unsigned i=0;i<3000;i++) {
        now_ms=1000+i/3;
        assert(SRNOS_CANRouterPostOneshot(4,&frame,0,20)==0);
        SRNOS_CANRouterCoreStepTx(4,now_ms);
    }
    assert(query_count==3000 && accepted==3000);
    puts("CAN4 actual backend/core: 3000 TX admissions/3000 FIFO queries, single-step budget, transient retry without IRQ, FIFO wait, TTL and clock wrap: PASS");
}
