#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include "IO/CAN/Native/fdcan_internal.h"
#include "fake_hal.h"
static unsigned rx_wakes,tx_wakes,tx_frames,busoff_wakes;
void SRNOS_FDCANOnRxISR(uint8_t bus) {assert(bus>0&&bus<=3);++rx_wakes;}
void SRNOS_FDCANOnTxEvent(uint8_t bus) {assert(bus==1);++tx_frames;}
void SRNOS_FDCANOnTxReady(uint8_t bus) {assert(bus==1);++tx_wakes;}
void SRNOS_FDCANOnBusoff(uint8_t bus) {assert(bus>0&&bus<=3);++busoff_wakes;}
static void receive(unsigned count) {rx_remaining=count;fake_isr=1;HAL_FDCAN_RxFifo0Callback(&fake_handles[0],FDCAN_IT_RX_FIFO0_NEW_MESSAGE);fake_isr=0;}
static void busoff(unsigned bus) {
 FDCAN_HandleTypeDef *h=&fake_handles[bus-1];h->Instance->PSR|=FDCAN_PSR_BO;h->Instance->CCCR|=FDCAN_CCCR_INIT;
 h->Instance->IR|=FDCAN_IT_BUS_OFF;assert(h->ErrorCode==HAL_FDCAN_ERROR_NONE);
 unsigned stops=stop_calls;fake_isr=1;HAL_FDCAN_IRQHandler(h);fake_isr=0;assert(stop_calls==stops);
 assert(!(h->Instance->IR&FDCAN_IT_BUS_OFF));
}
int main(void) {
 fake_freeze_classic();
 assert(SRNOS_FDCANInitAll()&&SRNOS_FDCANStartAll());
 receive(127);assert(rx_wakes==1);
 receive(33);assert(rx_wakes==1&&SRNOS_FDCANDiagnostics[0].rx_overrun_cnt==33);
 SRNOS_FDCANRxFrame out[32];unsigned total=0;size_t n;
 while((n=SRNOS_FDCANRxPop(1,out,32)))for(size_t i=0;i<n;i++) {assert(out[i].hdr.Identifier==total);for(unsigned k=0;k<8;k++)assert(out[i].data[k]==(uint8_t)total);++total;}
 assert(total==127);receive(7);assert(SRNOS_FDCANRxPop(1,out,32)==7&&out[0].hdr.Identifier==160);
 /* Even continuous arrivals cannot prolong one IRQ beyond its entry batch. */
 unsigned calls=rx_calls;refill_rx=true;receive(8);refill_rx=false;assert(rx_calls-calls==8);rx_remaining=0;
 assert(SRNOS_FDCANRxPop(1,out,32)==8);
 fail_rx=true;receive(1);fail_rx=false;rx_remaining=0;assert(SRNOS_FDCANRxPop(1,out,32)==0&&SRNOS_FDCANDiagnostics[0].rx_error_cnt==1);
 HAL_FDCAN_RxFifo0Callback(&fake_handles[0],FDCAN_IT_RX_FIFO0_MESSAGE_LOST);assert(SRNOS_FDCANDiagnostics[0].rx_hw_lost_cnt==1);
 fake_registers[0].TXEFS=8;fake_isr=1;HAL_FDCAN_TxEventFifoCallback(&fake_handles[0],FDCAN_IT_TX_EVT_FIFO_NEW_DATA);fake_isr=0;
 assert(tx_frames==8&&tx_wakes==1&&tx_event_calls==8);
 /* Actual pinned HAL ErrorStatus dispatch is compiled into this test. */
 fake_now=UINT32_MAX-15u;busoff(1);assert(busoff_wakes==1&&SRNOS_FDCANDiagnostics[0].bus_off_cnt==1&&!SRNOS_FDCANIsReady(1));
 assert(!SRNOS_FDCANService(1,fake_now));assert(SRNOS_FDCANNextServiceWait(1,fake_now)==100);
 assert(!SRNOS_FDCANService(1,fake_now+99)&&SRNOS_FDCANDiagnostics[0].recovery_attempts==0);
 fake_registers[0].TXBRP=3;abort_ack=false;
 assert(!SRNOS_FDCANService(1,fake_now+100)&&aborted_mask==3);assert(SRNOS_FDCANNextServiceWait(1,fake_now+100)==1);
 assert(!SRNOS_FDCANService(1,fake_now+109));fake_registers[0].TXBRP=0;
 assert(!SRNOS_FDCANService(1,fake_now+109));assert(SRNOS_FDCANDiagnostics[0].recovery_state==3);
 fake_registers[0].PSR=0;fake_registers[0].IR|=FDCAN_IT_BUS_OFF;fake_isr=1;HAL_FDCAN_IRQHandler(&fake_handles[0]);fake_isr=0;
 assert(SRNOS_FDCANService(1,fake_now+110));assert(SRNOS_FDCANDiagnostics[0].recovery_completed==1&&SRNOS_FDCANDiagnostics[0].bus_off_cnt==1);
 assert(SRNOS_FDCANNextServiceWait(1,fake_now+110)==UINT32_MAX);
 /* Failed HAL transition is retried only at deadlines, then latched. */
 fake_now=1000;busoff(2);fail_stop=true;assert(!SRNOS_FDCANService(2,1000));
 for(unsigned i=1;i<=3;i++)assert(!SRNOS_FDCANService(2,1000+100*i));
 assert(SRNOS_FDCANDiagnostics[1].recovery_attempts==3&&SRNOS_FDCANDiagnostics[1].recovery_faults==1);
 assert(SRNOS_FDCANNextServiceWait(2,1300)==UINT32_MAX&&!SRNOS_FDCANIsReady(2));
 unsigned old_stops=stop_calls;assert(!SRNOS_FDCANService(2,100000)&&stop_calls==old_stops);fail_stop=false;
 /* Recessive-bus confirmation also has a finite retry budget. */
 busoff(3);assert(!SRNOS_FDCANService(3,2000));
 for(unsigned i=1;i<=6;i++)assert(!SRNOS_FDCANService(3,2000+100*i));
 assert(SRNOS_FDCANDiagnostics[2].recovery_attempts==3&&SRNOS_FDCANDiagnostics[2].recovery_faults==1);
 puts("native HAL: SPSC drop-new, bounded IRQ, batched TX notification, real HAL status dispatch and bounded Bus-Off recovery PASS");
}
