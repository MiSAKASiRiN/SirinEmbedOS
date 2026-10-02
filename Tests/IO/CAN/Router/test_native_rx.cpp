#include <cassert>
#include <cstdio>
#include "../../../../Source/IO/CAN/Router/native_can_rx.cpp"
static SRNOS_NativeCANTaskCtx context;
static unsigned queued=70,routed,waits,yields,batch_count,max_batch,pops;
static uint32_t event_bits;
osEventFlagsId_t osEventFlagsNew(const void *) {return &event_bits;}
uint32_t osEventFlagsSet(osEventFlagsId_t,uint32_t bits) {return event_bits|=bits;}
uint32_t osEventFlagsWait(osEventFlagsId_t,uint32_t bits,uint32_t options,uint32_t timeout) {
 assert(options==osFlagsWaitAny&&timeout==osWaitForever);
 if(batch_count>max_batch)max_batch=batch_count;batch_count=0;++waits;
 if(!event_bits){assert(!queued);throw 1;}
 uint32_t result=event_bits&bits;event_bits&=~bits;return result;
}
int osThreadYield(void) {++yields;return 0;}
bool SRNOS_FDCANStart(uint8_t bus) {assert(bus==1);return true;}
size_t SRNOS_FDCANRxPop(uint8_t bus,SRNOS_FDCANRxFrame *out,size_t max) {
 assert(bus==1&&max<=32);++pops;size_t n=queued<max?queued:max;queued-=n;
 for(size_t i=0;i<n;i++){out[i]={};out[i].hdr.Identifier=0x55;out[i].hdr.DataLength=8;}
 return n;
}
extern "C" SRNOS_NativeCANTaskCtx *SRNOS_NativeCANTaskCtxOf(SRNOS_CANChannel bus) {assert(bus==1);return &context;}
extern "C" void SRNOS_NativeCANBackendEnsureBound(SRNOS_CANChannel) {}
extern "C" uint32_t SRNOS_NativeCANBackendNowMs(void) {return 10;}
extern "C" void SRNOS_NativeCANBackendSignalRx(SRNOS_CANChannel) {event_bits|=SRNOS_NATIVE_CAN_EVT_RX;}
extern "C" void SRNOS_CANRouterCoreNoteRxWake(SRNOS_CANChannel,bool by_isr) {assert(by_isr);}
extern "C" void SRNOS_CANRouterCoreOnRxFrame(SRNOS_CANChannel,const SRNOS_CANFrame *frame,uint32_t) {
 assert(frame->id==0x55&&frame->dlc==8);++routed;++batch_count;
}
int main() {
 context.pending_rx=SRNOS_NATIVE_CAN_EVT_RX;
 try {SRNOS_NativeCANRxRouterWorker((void *)1);}catch(int end){assert(end==1);}
 assert(routed==70&&pops==3&&max_batch==32&&yields==2&&waits==4);
 std::puts("native CAN RX: bounded 32-frame batches, retained readiness, real yield and idle blocking PASS");
}
