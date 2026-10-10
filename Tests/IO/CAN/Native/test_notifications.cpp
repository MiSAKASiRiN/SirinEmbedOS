#include <cassert>
#include <cstdio>
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
static unsigned notify_task,notify_isr,rx_yields,routed,rx_batch,max_batch,tx_steps,tx_completed;
static unsigned rx_waits;
static bool inject_rx=true,inject_tx=true;
struct Finished {};
TaskHandle_t xTaskGetCurrentTaskHandle(void) {return current;}
BaseType_t xTaskNotify(TaskHandle_t thread,uint32_t bits,eNotifyAction action) {
 assert(!fake_isr&&action==eSetBits);++notify_task;*static_cast<uint32_t *>(thread)|=bits;return pdPASS;
}
BaseType_t xTaskNotifyFromISR(TaskHandle_t thread,uint32_t bits,eNotifyAction action,BaseType_t *higher) {
 assert(fake_isr&&action==eSetBits);++notify_isr;*higher=pdTRUE;*static_cast<uint32_t *>(thread)|=bits;return pdPASS;
}
BaseType_t xTaskNotifyWait(uint32_t clear_entry,uint32_t clear_exit,uint32_t *bits,TickType_t timeout) {
 assert(clear_entry==0&&clear_exit==UINT32_MAX&&timeout==portMAX_DELAY);
 auto *value=static_cast<uint32_t *>(current);
 if(current==&notifications[0]) {
  const unsigned expected[]={32u,32u,6u,1u};
  assert(rx_waits<4u&&rx_batch==expected[rx_waits++]);
  if(rx_batch>max_batch)max_batch=rx_batch;rx_batch=0;
  if(!*value&&inject_rx) {inject_rx=false;fake_isr=1;rx_remaining=1;HAL_FDCAN_RxFifo0Callback(&fake_handles[0],FDCAN_IT_RX_FIFO0_NEW_MESSAGE);fake_isr=0;}
 } else if(!*value&&inject_tx) {
  inject_tx=false;fake_isr=1;fake_registers[0].TXEFS=8;
  HAL_FDCAN_TxEventFifoCallback(&fake_handles[0],FDCAN_IT_TX_EVT_FIFO_NEW_DATA);fake_isr=0;
 }
 if(!*value)throw Finished{};
 *bits=*value;*value=0;return pdTRUE;
}
int osThreadYield(void) {++rx_yields;return 0;}
extern "C" void SRNOS_CANRouterCoreSetBackend(SRNOS_CANChannel ch,const SRNOS_CANRouterBackendOps *ops) {assert(ch==1&&ops);}
extern "C" uint32_t SRNOS_CANRouterCoreNextTxWait(SRNOS_CANChannel ch,uint32_t) {assert(ch==1);return UINT32_MAX;}
extern "C" void SRNOS_CANRouterCoreDrainTx(SRNOS_CANChannel ch,uint32_t) {assert(ch==1);++tx_steps;}
extern "C" void SRNOS_CANRouterCoreNoteTxComplete(SRNOS_CANChannel ch) {assert(ch==1);++tx_completed;}
extern "C" void SRNOS_CANRouterCoreNoteRxWake(SRNOS_CANChannel ch,bool) {assert(ch==1);}
extern "C" void SRNOS_CANRouterCoreNoteRxRejected(SRNOS_CANChannel) {assert(false);}
extern "C" void SRNOS_CANFDCoreSetBackend(SRNOS_CANChannel,const SRNOS_CANFDBackendOps *) {assert(false);}
extern "C" uint32_t SRNOS_CANFDCoreNextTxWait(SRNOS_CANChannel,uint32_t) {assert(false);return 0;}
extern "C" void SRNOS_CANFDCoreDrainTx(SRNOS_CANChannel,uint32_t) {assert(false);}
extern "C" void SRNOS_CANFDCoreNoteTxComplete(SRNOS_CANChannel) {assert(false);}
extern "C" void SRNOS_CANFDCoreNoteRxWake(SRNOS_CANChannel,bool) {assert(false);}
extern "C" void SRNOS_CANFDCoreNoteRxRejected(SRNOS_CANChannel) {assert(false);}
extern "C" void SRNOS_CANFDCoreOnRxFrame(SRNOS_CANChannel,const SRNOS_CANFDFrame *,uint32_t) {assert(false);}
extern "C" void SRNOS_CANRouterCoreOnRxFrame(SRNOS_CANChannel ch,const SRNOS_CANFrame *frame,uint32_t) {
 assert(ch==1&&frame->id==routed&&frame->dlc==8);++routed;++rx_batch;
}
int main() {
 fake_freeze_classic();
 assert(SRNOS_FDCANInitAll()&&SRNOS_FDCANStartAll());
 /* IRQ before worker publication: initial step, rather than allocation-time
  * event handles or an unsynchronized pending bit, consumes these 70 frames. */
 fake_isr=1;rx_remaining=70;HAL_FDCAN_RxFifo0Callback(&fake_handles[0],FDCAN_IT_RX_FIFO0_NEW_MESSAGE);fake_isr=0;
 assert(notify_isr==0);
 current=&notifications[0];
 try {SRNOS_NativeCANRxRouterWorker((void *)1);}catch(Finished &) {}
 assert(routed==71&&pop_calls==19&&max_batch==32&&rx_waits==4&&rx_yields==2&&notify_task==2&&notify_isr==1);
 current=&notifications[1];
 try {SRNOS_NativeCANTxRouterWorker((void *)1);}catch(Finished &) {}
 assert(tx_steps==2&&tx_completed==8&&notify_isr==2);
 puts("native workers: direct ISR notify, startup backlog, wait race, 32-frame batches and 8 completions / 1 wake PASS");
}
