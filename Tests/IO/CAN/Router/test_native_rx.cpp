#include <cassert>
#include <cstdio>
#include "../../../../Source/IO/CAN/Router/native_can_rx.cpp"
static SRNOS_NativeCANTaskCtx context;
static unsigned queued=70,routed,waits,yields,batch_count,max_batch,pops;
static uint32_t event_bits;
TaskHandle_t xTaskGetCurrentTaskHandle(void) {return &event_bits;}
BaseType_t xTaskNotifyWait(uint32_t clear_in,uint32_t clear_out,uint32_t *bits,TickType_t timeout) {
 assert(clear_in==0 && clear_out==UINT32_MAX && timeout==portMAX_DELAY);
 const unsigned expected[]={32u,32u,6u};
 assert(waits<3u&&batch_count==expected[waits]);
 if(batch_count>max_batch)max_batch=batch_count;batch_count=0;++waits;
 if(!event_bits){assert(!queued);throw 1;}
 *bits=event_bits;event_bits=0;return pdTRUE;
}
int osThreadYield(void) {++yields;return 0;}
bool SRNOS_FDCANStart(uint8_t bus) {assert(bus==1);return true;}
size_t SRNOS_FDCANRxPop(uint8_t bus,SRNOS_FDCANRxFrame *out,size_t max) {
 assert(bus==1&&max<=4);++pops;size_t n=queued<max?queued:max;queued-=n;
 for(size_t i=0;i<n;i++){out[i]={};out[i].hdr.Identifier=0x55;out[i].hdr.DataLength=8;}
 return n;
}
extern "C" SRNOS_NativeCANTaskCtx *SRNOS_NativeCANTaskCtxOf(SRNOS_CANChannel bus) {assert(bus==1);return &context;}
extern "C" void SRNOS_NativeCANBackendEnsureBound(SRNOS_CANChannel) {}
extern "C" uint32_t SRNOS_NativeCANBackendNowMs(void) {return 10;}
extern "C" void SRNOS_NativeCANBackendSignalRx(SRNOS_CANChannel) {event_bits|=SRNOS_NATIVE_CAN_EVT_RX;}
extern "C" void SRNOS_CANRouterCoreNoteRxWake(SRNOS_CANChannel,bool) {}
extern "C" void SRNOS_CANRouterCoreNoteRxRejected(SRNOS_CANChannel) {assert(false);}
extern "C" void SRNOS_CANFDCoreNoteRxWake(SRNOS_CANChannel,bool) {assert(false);}
extern "C" void SRNOS_CANFDCoreNoteRxRejected(SRNOS_CANChannel) {assert(false);}
extern "C" void SRNOS_CANFDCoreOnRxFrame(SRNOS_CANChannel,const SRNOS_CANFDFrame *,uint32_t) {assert(false);}
extern "C" void SRNOS_CANRouterCoreOnRxFrame(SRNOS_CANChannel,const SRNOS_CANFrame *frame,uint32_t) {
 assert(frame->id==0x55&&frame->dlc==8);++routed;++batch_count;
}
int main() {
 /* No notification before registration: initial pass must consume backlog. */
 try {SRNOS_NativeCANRxRouterWorker((void *)1);}catch(int end){assert(end==1);}
 assert(routed==70&&pops==18&&max_batch==32&&yields==2&&waits==3);
 std::puts("native CAN RX: 4-frame pop chunks, bounded 32-frame batches, retained readiness, real yield and idle blocking PASS");
}
