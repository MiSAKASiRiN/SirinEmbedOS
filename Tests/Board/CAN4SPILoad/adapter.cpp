#include "adapter.h"
#include "IO/CAN/External/EXFDCAN_Router_Internal.h"
#include <cstring>
extern "C" int SRNOS_TestCAN4Ready(void) {
    auto *ctx=SRNOS_ExternalCANCtxOf(0);return ctx && ctx->init_done && ctx->task;
}
extern "C" void SRNOS_TestCAN4HardwareRead(SRNOS_TestCAN4Hardware *out) {
    SRNOS_ExternalCANHardware snapshot{};
    (void)SRNOS_ExternalCANReadHardware(0,&snapshot);
    static_assert(sizeof(snapshot)==sizeof(*out));std::memcpy(out,&snapshot,sizeof(snapshot));
}

extern "C" void SRNOS_TestCAN4PerformanceRead(uint32_t out[10]) {
    auto *ctx=SRNOS_ExternalCANCtxOf(0);
    uint32_t key=__get_PRIMASK();__disable_irq();
    out[0]=ctx->wakeups;out[1]=ctx->rounds;out[2]=ctx->rx_service_us;out[3]=ctx->tx_service_us;
    out[4]=ctx->max_rx_us;out[5]=ctx->max_tx_us;
    out[6]=ctx->rx_fifo.uncertain;out[7]=ctx->tx_fifo.uncertain;
    out[8]=SRNOS_DebugDbgSPI[0].xfer_start;out[9]=SRNOS_DebugDbgSPI[0].xfer_done;
    __set_PRIMASK(key);
}
