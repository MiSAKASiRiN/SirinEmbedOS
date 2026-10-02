#include <cassert>
#include <cstdint>
#include <cstdio>
#include "../../../../Source/IO/CAN/External/exfdcan_tx_router.cpp"

static uint32_t now_ms, query_count, fail_query_at, accepted, signals;
static bool fail_load, fail_all_queries, fifo_full;
static SRNOS_ExternalCANStats hw_stats;
static SRNOS_ExternalCANCtx context;
extern "C" uint32_t HAL_GetTick(void) { return now_ms; }
extern "C" int SRNOS_ExternalCANDevFromChannel(SRNOS_CANChannel ch) { return ch==4?0:-1; }
extern "C" SRNOS_CANChannel SRNOS_ExternalCANChannelFromDev(uint8_t dev) { return dev==0?4:0; }
extern "C" SRNOS_ExternalCANCtx *SRNOS_ExternalCANCtxOf(uint8_t dev) { return dev==0?&context:nullptr; }
extern "C" int SRNOS_ExternalCANHwInit(uint8_t) { return 0; }
extern "C" void SRNOS_ExternalCANBusLock(void) {}
extern "C" void SRNOS_ExternalCANBusUnlock(void) {}
osEventFlagsId_t osEventFlagsNew(const void *) { return &context; }
uint32_t osEventFlagsSet(osEventFlagsId_t,uint32_t bits) { ++signals;return bits; }
uint32_t osEventFlagsWait(osEventFlagsId_t,uint32_t,uint32_t,uint32_t) { assert(false);return 0; }
extern "C" int8_t DRV_CANFDSPI_TransmitChannelStatusGet(CANFDSPI_MODULE_ID,CAN_FIFO_CHANNEL,CAN_TX_FIFO_STATUS *status)
{
    ++query_count;
    if(fail_all_queries || query_count==fail_query_at)return -1;
    *status=fifo_full?CAN_TX_FIFO_FULL:CAN_TX_FIFO_NOT_FULL;return 0;
}
extern "C" int8_t DRV_CANFDSPI_TransmitChannelLoad(CANFDSPI_MODULE_ID,CAN_FIFO_CHANNEL,CAN_TX_MSGOBJ *,uint8_t *,uint32_t,bool)
{
    if(fail_load) { fail_load=false;return -1; }
    ++accepted;return 0;
}
extern "C" int8_t DRV_CANFDSPI_ModuleEventGet(CANFDSPI_MODULE_ID,CAN_MODULE_EVENT *events) { *events=CAN_NO_EVENT;return 0; }
extern "C" int8_t DRV_CANFDSPI_ModuleEventClear(CANFDSPI_MODULE_ID,CAN_MODULE_EVENT) { return 0; }
extern "C" int8_t DRV_CANFDSPI_TransmitChannelEventAttemptClear(CANFDSPI_MODULE_ID,CAN_FIFO_CHANNEL) { return 0; }

static void post(uint32_t at)
{
    now_ms=at;context={};context.stats=&hw_stats;context.evt=&context;
    query_count=fail_query_at=accepted=signals=0;fail_load=fail_all_queries=fifo_full=false;
    SRNOS_CANFrame frame{};frame.id=0x123;frame.dlc=8;
    assert(SRNOS_CANRouterCoreNextTxWait(4,now_ms)==UINT32_MAX);
    assert(SRNOS_CANRouterPostOneshot(4,&frame,0,20)==0);
    assert(signals==1);
}
static void check_retry_without_irq()
{
    SRNOS_CANRouterCoreDrainTx(4,now_ms);
    assert(accepted==0 && context.tx_retry_pending);
    assert(SRNOS_CANRouterCoreNextTxWait(4,now_ms)==1);
    ++now_ms;
    assert(SRNOS_CANRouterCoreNextTxWait(4,now_ms)==0);
    SRNOS_CANRouterCoreDrainTx(4,now_ms);
    assert(accepted==1 && !context.tx_retry_pending && signals==1);
    assert(SRNOS_CANRouterCoreNextTxWait(4,now_ms)==UINT32_MAX);
}
int main()
{
    SRNOS_ExternalCANBackendBind(0);
    post(100);fail_query_at=1;check_retry_without_irq();
    post(200);fail_query_at=2;check_retry_without_irq();
    post(300);fail_load=true;check_retry_without_irq();
    post(UINT32_MAX);fail_query_at=1;check_retry_without_irq();
    /* Actual FIFO full still waits for TX IRQ or TTL, with no forced tick. */
    post(400);fifo_full=true;SRNOS_CANRouterCoreDrainTx(4,now_ms);
    assert(!context.tx_retry_pending && SRNOS_CANRouterCoreNextTxWait(4,now_ms)==20);
    now_ms+=20;SRNOS_CANRouterCoreDrainTx(4,now_ms);
    assert(!accepted && SRNOS_CANRouterCoreNextTxWait(4,now_ms)==UINT32_MAX);
    /* Persistent SPI failure cannot extend the oneshot lifetime or leave an
     * idle 1 kHz wake once the queue is empty. */
    post(500);fail_all_queries=true;
    for(unsigned i=0;i<20;i++) {
        SRNOS_CANRouterCoreDrainTx(4,now_ms);
        assert(SRNOS_CANRouterCoreNextTxWait(4,now_ms)==1);++now_ms;
    }
    SRNOS_CANRouterCoreDrainTx(4,now_ms);
    assert(!accepted && SRNOS_CANRouterCoreNextTxWait(4,now_ms)==UINT32_MAX);
    puts("CAN4 actual backend/core: transient SPI query/load retry without IRQ, FIFO wait, TTL and clock wrap: PASS");
}
