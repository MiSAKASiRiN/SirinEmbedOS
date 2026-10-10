#include <cassert>
#include <cstdio>
#include <cstring>
extern "C" {
#include "fake_hal.h"
}
#include "../../../../Source/IO/CAN/Router/native_can_tx.cpp"
static unsigned pop_calls;
static size_t checked_pop(uint8_t bus,SRNOS_FDCANRxFrame *frames,size_t max)
{assert(max<=4u);++pop_calls;return SRNOS_FDCANRxPop(bus,frames,max);}
#define SRNOS_FDCANRxPop checked_pop
#include "../../../../Source/IO/CAN/Router/native_can_rx.cpp"
#undef SRNOS_FDCANRxPop

static uint32_t notifications[2];
static TaskHandle_t current;
static unsigned isr_notifications,rx_yields,rx_count,rx_batch,max_batch,tx_steps,tx_completed;
static unsigned rx_waits;
static unsigned fd_rejected,classic_rejected,classic_received,classic_completed;
static bool inject_tx=true,worker_active;
static SRNOS_CANFDFrame last_rx;
static const SRNOS_CANFDBackendOps *bound_fd;
struct Finished {};
TaskHandle_t xTaskGetCurrentTaskHandle(void) {return current;}
BaseType_t xTaskNotify(TaskHandle_t thread,uint32_t bits,eNotifyAction action) {
    assert(!fake_isr&&action==eSetBits);*static_cast<uint32_t *>(thread)|=bits;return pdPASS;
}
BaseType_t xTaskNotifyFromISR(TaskHandle_t thread,uint32_t bits,eNotifyAction action,BaseType_t *higher) {
    assert(fake_isr&&action==eSetBits);++isr_notifications;*higher=pdTRUE;
    *static_cast<uint32_t *>(thread)|=bits;return pdPASS;
}
BaseType_t xTaskNotifyWait(uint32_t clear_entry,uint32_t clear_exit,uint32_t *bits,TickType_t timeout) {
    assert(clear_entry==0u&&clear_exit==UINT32_MAX&&timeout==portMAX_DELAY);
    auto *value=static_cast<uint32_t *>(current);
    if(current==&notifications[0]) {
        const unsigned expected[]={32u,32u,6u};
        assert(rx_waits<3u&&rx_batch==expected[rx_waits++]);
        if(rx_batch>max_batch)max_batch=rx_batch;
        rx_batch=0;
    } else if(!*value&&inject_tx) {
        inject_tx=false;fake_isr=1;fake_registers[0].TXEFS=8;
        HAL_FDCAN_TxEventFifoCallback(&fake_handles[0],FDCAN_IT_TX_EVT_FIFO_NEW_DATA);fake_isr=0;
    }
    if(!*value)throw Finished{};
    *bits=*value;*value=0;return pdTRUE;
}
int osThreadYield(void) {++rx_yields;return 0;}
extern "C" void SRNOS_CANRouterCoreSetBackend(SRNOS_CANChannel,const SRNOS_CANRouterBackendOps *) {assert(false);}
extern "C" uint32_t SRNOS_CANRouterCoreNextTxWait(SRNOS_CANChannel,uint32_t) {assert(false);return 0;}
extern "C" void SRNOS_CANRouterCoreDrainTx(SRNOS_CANChannel,uint32_t) {assert(false);}
extern "C" void SRNOS_CANRouterCoreNoteTxComplete(SRNOS_CANChannel ch) {assert(ch==2u);++classic_completed;}
extern "C" void SRNOS_CANRouterCoreNoteRxWake(SRNOS_CANChannel,bool) {assert(false);}
extern "C" void SRNOS_CANRouterCoreNoteRxRejected(SRNOS_CANChannel ch) {assert(ch==2u);++classic_rejected;}
extern "C" void SRNOS_CANRouterCoreOnRxFrame(SRNOS_CANChannel ch,const SRNOS_CANFrame *frame,uint32_t) {
    assert(ch==2u&&frame->dlc==8u);++classic_received;
}
extern "C" void SRNOS_CANFDCoreSetBackend(SRNOS_CANChannel ch,const SRNOS_CANFDBackendOps *ops) {assert(ch==1u);bound_fd=ops;}
extern "C" uint32_t SRNOS_CANFDCoreNextTxWait(SRNOS_CANChannel ch,uint32_t) {assert(ch==1u);return UINT32_MAX;}
extern "C" void SRNOS_CANFDCoreDrainTx(SRNOS_CANChannel ch,uint32_t) {assert(ch==1u);++tx_steps;}
extern "C" void SRNOS_CANFDCoreNoteTxComplete(SRNOS_CANChannel ch) {assert(ch==1u);++tx_completed;}
extern "C" void SRNOS_CANFDCoreNoteRxWake(SRNOS_CANChannel ch,bool) {assert(ch==1u);}
extern "C" void SRNOS_CANFDCoreNoteRxRejected(SRNOS_CANChannel ch) {assert(ch==1u);++fd_rejected;}
extern "C" void SRNOS_CANFDCoreOnRxFrame(SRNOS_CANChannel ch,const SRNOS_CANFDFrame *frame,uint32_t) {
    assert(ch==1u);last_rx=*frame;
    if(worker_active) {
        assert(frame->length==64u);
        for(unsigned k=0;k<64u;++k)assert(frame->data[k]==(uint8_t)(rx_count+k));
    }
    ++rx_count;++rx_batch;
}
int main()
{
    SRNOS_CANControllerConfig configs[4];
    for(auto &c:configs)c={SRNOS_CAN_MODE_CLASSIC,1000000u,0u,false};
    configs[0]={SRNOS_CAN_MODE_FD,1000000u,1000000u,false};
    assert(SRNOS_CANControllerPrepare(configs));fake_config_frozen=true;
    assert(SRNOS_FDCANInitAll()&&SRNOS_FDCANStartAll());
    custom_rx=true;custom_rx_header.Identifier=0x18abcdefu;custom_rx_header.IdType=FDCAN_EXTENDED_ID;
    custom_rx_header.FDFormat=FDCAN_FD_CAN;custom_rx_header.DataLength=15u;
    custom_rx_header.BitRateSwitch=FDCAN_BRS_ON;custom_rx_header.ErrorStateIndicator=FDCAN_ESI_PASSIVE;
    rx_remaining=70;fake_isr=1;
    HAL_FDCAN_RxFifo0Callback(&fake_handles[0],FDCAN_IT_RX_FIFO0_NEW_MESSAGE);fake_isr=0;
    assert(isr_notifications==0);
    worker_active=true;current=&notifications[0];
    try {SRNOS_NativeCANRxRouterWorker((void *)1);}catch(Finished &) {}
    worker_active=false;
    assert(rx_count==70u&&pop_calls==18u&&max_batch==32u&&rx_waits==3u&&rx_yields==2u);
    assert(last_rx.flags==(SRNOS_CANFD_FRAME_FLAG_EXT|SRNOS_CANFD_FRAME_FLAG_BRS|SRNOS_CANFD_FRAME_FLAG_ESI));

    // Every wire DLC, including FD <=8, reaches only the FD endpoint unchanged.
    SRNOS_FDCANRxFrame in{};in.hdr=custom_rx_header;
    for(unsigned k=0;k<64u;++k)in.data[k]=(uint8_t)(k^0x5au);
    for(uint8_t dlc=0u;dlc<16u;++dlc) {
        in.hdr.DataLength=dlc;in.hdr.BitRateSwitch=(dlc&1u)?FDCAN_BRS_ON:FDCAN_BRS_OFF;
        route_rx_frame(1u,&in);
        assert(last_rx.length==SRNOS_CANFDDlcToLength(dlc));
        assert(((last_rx.flags&SRNOS_CANFD_FRAME_FLAG_BRS)!=0u)==((dlc&1u)!=0u));
        assert(last_rx.flags&SRNOS_CANFD_FRAME_FLAG_ESI);
        assert(std::memcmp(last_rx.data,in.data,last_rx.length)==0);
        for(unsigned k=last_rx.length;k<64u;++k)assert(last_rx.data[k]==0u);
    }
    in.hdr.DataLength=8u;route_rx_frame(2u,&in);assert(classic_rejected==1u&&classic_received==0u);
    in.hdr.FDFormat=FDCAN_CLASSIC_CAN;route_rx_frame(1u,&in);assert(fd_rejected==1u);
    route_rx_frame(2u,&in);assert(classic_received==1u);
    in.hdr.FDFormat=FDCAN_FD_CAN;in.hdr.DataLength=16u;route_rx_frame(1u,&in);
    in.hdr.DataLength=8u;in.hdr.RxFrameType=FDCAN_REMOTE_FRAME;route_rx_frame(1u,&in);
    assert(fd_rejected==3u);

    current=&notifications[1];
    try {SRNOS_NativeCANTxRouterWorker((void *)1);}catch(Finished &) {}
    assert(tx_steps==2u&&tx_completed==8u&&isr_notifications==1u&&bound_fd);
    SRNOS_FDCANOnTxEvent(2u);assert(classic_completed==1u);
    SRNOS_CANFDFrame tx{};tx.id=0x123u;tx.length=64u;
    for(unsigned k=0;k<64u;++k)tx.data[k]=(uint8_t)k;
    fake_registers[0].TXFQS=1u;
    assert(bound_fd->get_tx_free(1u)==1u&&bound_fd->try_push(1u,&tx));
    assert(captured_header.FDFormat==FDCAN_FD_CAN&&captured_header.BitRateSwitch==FDCAN_BRS_OFF);
    assert(std::memcmp(captured_data,tx.data,64u)==0);
    puts("native FD workers: 64-byte startup backlog/32-frame batches, all DLCs, mode/BRS/ESI isolation and completion routing PASS");
}
