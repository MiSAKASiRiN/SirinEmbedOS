#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "../../../../Source/IO/CAN/Router/native_can_tx.cpp"
#include "../../../../Source/IO/CAN/Router/native_can_rx.cpp"
static uint32_t app_ms=100;
static bool fd_mode;
static unsigned fd_routed;
static FDCAN_GlobalTypeDef instances[3];
FDCAN_GlobalTypeDef *FDCAN1=&instances[0], *FDCAN2=&instances[1], *FDCAN3=&instances[2];
extern "C" uint32_t HAL_GetTick(void) {return app_ms+12u;}
extern "C" uint32_t SRNOS_TimeMs(void) {return app_ms;}
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
bool SRNOS_FDCANStart(uint8_t bus) {assert(bus==1 || bus==2);return true;}
size_t SRNOS_FDCANRxPop(uint8_t bus,SRNOS_FDCANRxFrame *out,size_t max) {
 assert(bus==1&&max<=4);++pops;size_t n=queued<max?queued:max;queued-=n;
 for(size_t i=0;i<n;i++){out[i]={};out[i].hdr.Identifier=0x55;out[i].hdr.DataLength=8;}
 return n;
}
extern "C" void SRNOS_CANRouterCoreNoteRxWake(SRNOS_CANChannel,bool) {}
extern "C" void SRNOS_CANRouterCoreNoteRxRejected(SRNOS_CANChannel) {assert(false);}
extern "C" void SRNOS_CANFDCoreNoteRxWake(SRNOS_CANChannel,bool) {assert(false);}
extern "C" void SRNOS_CANFDCoreNoteRxRejected(SRNOS_CANChannel) {assert(false);}
extern "C" void SRNOS_CANFDCoreOnRxFrame(SRNOS_CANChannel,const SRNOS_CANFDFrame *,uint32_t stamp) {assert(fd_mode && stamp==app_ms);++fd_routed;}
extern "C" void SRNOS_CANRouterCoreOnRxFrame(SRNOS_CANChannel,const SRNOS_CANFrame *frame,uint32_t stamp) {
 assert(stamp==app_ms);assert(frame->id==0x55&&frame->dlc==8);++routed;++batch_count;
}
BaseType_t xTaskNotify(TaskHandle_t target,uint32_t bits,eNotifyAction) {
 assert(target==&event_bits);event_bits|=bits;return pdTRUE;
}
BaseType_t xTaskNotifyFromISR(TaskHandle_t,uint32_t,eNotifyAction,BaseType_t*) {assert(false);return pdFALSE;}
bool SRNOS_FDCANIsReady(uint8_t) {return true;}
bool SRNOS_FDCANTxPush(uint8_t,uint16_t,const uint8_t*,uint8_t) {return true;}
bool SRNOS_FDCANTxPushExt(uint8_t,uint32_t,const uint8_t*,uint8_t) {return true;}
bool SRNOS_FDCANTxPushFD(SRNOS_CANChannel,const SRNOS_CANFDFrame*) {return true;}
bool SRNOS_FDCANService(uint8_t,uint32_t stamp) {assert(stamp==app_ms);return true;}
uint32_t SRNOS_FDCANNextServiceWait(uint8_t,uint32_t stamp) {assert(stamp==app_ms);return UINT32_MAX;}
extern "C" void SRNOS_CANRouterCoreSetBackend(SRNOS_CANChannel,const SRNOS_CANRouterBackendOps *ops) {assert(ops->now_ms()==app_ms);}
extern "C" void SRNOS_CANFDCoreSetBackend(SRNOS_CANChannel,const SRNOS_CANFDBackendOps *ops) {assert(ops->now_ms()==app_ms);}
extern "C" void SRNOS_CANRouterCoreNoteTxComplete(SRNOS_CANChannel) {}
extern "C" void SRNOS_CANFDCoreNoteTxComplete(SRNOS_CANChannel) {}
extern "C" uint32_t SRNOS_CANRouterCoreNextTxWait(SRNOS_CANChannel,uint32_t stamp) {assert(stamp==app_ms);throw 2;}
extern "C" uint32_t SRNOS_CANFDCoreNextTxWait(SRNOS_CANChannel,uint32_t stamp) {assert(stamp==app_ms);throw 2;}
extern "C" void SRNOS_CANRouterCoreDrainTx(SRNOS_CANChannel,uint32_t stamp) {assert(stamp==app_ms);}
extern "C" void SRNOS_CANFDCoreDrainTx(SRNOS_CANChannel,uint32_t stamp) {assert(stamp==app_ms);}
int main() {
 /* No notification before registration: initial pass must consume backlog. */
 try {SRNOS_NativeCANRxRouterWorker((void *)1);}catch(int end){assert(end==1);}
 assert(routed==70&&pops==18&&max_batch==32&&yields==2&&waits==3);
 // Actual backend and both RX/TX workers must stay on the public clock,
 // including zero at startup and wrap, while HAL leads by 12 ms.
 const SRNOS_CANControllerConfig classic{SRNOS_CAN_MODE_CLASSIC,1000000u,0u,false};
 const SRNOS_CANControllerConfig fd_config{SRNOS_CAN_MODE_FD,1000000u,1000000u,false};
 const SRNOS_CANControllerConfig configs[4]={classic,fd_config,classic,classic};
 assert(SRNOS_CANControllerPrepare(configs));
 for(uint32_t stamp : {0u,100u,UINT32_MAX,0u}) {
  app_ms=stamp;assert(SRNOS_NativeCANBackendNowMs()==app_ms);
  for(bool fd : {false,true}) {
   fd_mode=fd;const SRNOS_CANChannel ch=fd?2:1;
   SRNOS_FDCANRxFrame frame{};frame.hdr.Identifier=0x55;frame.hdr.DataLength=8;
   frame.hdr.FDFormat=fd?FDCAN_FD_CAN:FDCAN_CLASSIC_CAN;
   route_rx_frame(ch,&frame);
   try {SRNOS_NativeCANTxRouterWorker((void*)(uintptr_t)ch);}catch(int end){assert(end==2);}
  }
 }
 assert(fd_routed==4);
 std::puts("native CAN RX: 4-frame pop chunks, bounded 32-frame batches, retained readiness, real yield, idle blocking, Classic/FD RX/TX public clock and rollover PASS");
}
