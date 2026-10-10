#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include "fake_hal.h"
#include "IO/CAN/can_controller_internal.h"
FDCAN_GlobalTypeDef fake_registers[3];
FDCAN_ClockCalibrationUnit_TypeDef fake_ccu;
FDCAN_HandleTypeDef fake_handles[3]={
 {.Instance=&fake_registers[0],.State=HAL_FDCAN_STATE_READY},
 {.Instance=&fake_registers[1],.State=HAL_FDCAN_STATE_READY},
 {.Instance=&fake_registers[2],.State=HAL_FDCAN_STATE_READY}};
uint32_t fake_now,fake_isr,rx_remaining,rx_sequence;
unsigned stop_calls,start_calls,abort_calls,rx_calls,tx_event_calls;
bool fail_rx,refill_rx,fail_stop,abort_ack=true,clear_bo_on_start;
uint32_t aborted_mask;
uint32_t fake_clock=80000000u,tdc_offset[3],tdc_filter[3];
bool tdc_enabled[3],custom_rx;
unsigned init_calls,tx_calls,fail_init_bus,fail_tdc_bus,fail_filter_bus;
FDCAN_TxHeaderTypeDef captured_header;
uint8_t captured_data[64];
FDCAN_RxHeaderTypeDef custom_rx_header;
FDCAN_HandleTypeDef *SRNOS_BoardCANHandle(uint8_t bus) {return bus>0&&bus<=3?&fake_handles[bus-1]:NULL;}
uint32_t HAL_GetTick(void) {return fake_now;}
uint32_t __get_IPSR(void) {return fake_isr;}
uint32_t HAL_RCCEx_GetPeriphCLKFreq(uint32_t clock) {assert(clock==RCC_PERIPHCLK_FDCAN);return fake_clock;}
HAL_StatusTypeDef HAL_FDCAN_Init(FDCAN_HandleTypeDef *h) {
 assert(!fake_isr&&h->State==HAL_FDCAN_STATE_READY);++init_calls;
 if(fail_init_bus==(unsigned)(h-fake_handles)+1u)return HAL_ERROR;
 h->Instance->CCCR|=FDCAN_CCCR_INIT;return HAL_OK;
}
HAL_StatusTypeDef HAL_FDCAN_ConfigTxDelayCompensation(FDCAN_HandleTypeDef *h,uint32_t offset,uint32_t filter) {
 unsigned i=(unsigned)(h-fake_handles);assert(!fake_isr&&h->State==HAL_FDCAN_STATE_READY&&offset<=127u&&filter<=127u);
 if(fail_tdc_bus==i+1u)return HAL_ERROR;tdc_offset[i]=offset;tdc_filter[i]=filter;return HAL_OK;
}
HAL_StatusTypeDef HAL_FDCAN_EnableTxDelayCompensation(FDCAN_HandleTypeDef *h) {
 unsigned i=(unsigned)(h-fake_handles);assert(!fake_isr&&h->State==HAL_FDCAN_STATE_READY);tdc_enabled[i]=true;return HAL_OK;
}
HAL_StatusTypeDef HAL_FDCAN_DisableTxDelayCompensation(FDCAN_HandleTypeDef *h) {
 unsigned i=(unsigned)(h-fake_handles);assert(!fake_isr&&h->State==HAL_FDCAN_STATE_READY);
 if(fail_tdc_bus==i+1u)return HAL_ERROR;tdc_enabled[i]=false;return HAL_OK;
}
HAL_StatusTypeDef HAL_FDCAN_ConfigFilter(FDCAN_HandleTypeDef *h,FDCAN_FilterTypeDef *f) {(void)h;(void)f;return HAL_OK;}
HAL_StatusTypeDef HAL_FDCAN_ConfigGlobalFilter(FDCAN_HandleTypeDef *h,uint32_t a,uint32_t b,uint32_t c,uint32_t d) {(void)a;(void)b;(void)c;(void)d;return fail_filter_bus==(unsigned)(h-fake_handles)+1u?HAL_ERROR:HAL_OK;}
HAL_StatusTypeDef HAL_FDCAN_ActivateNotification(FDCAN_HandleTypeDef *h,uint32_t bits,uint32_t buffers) {(void)buffers;h->Instance->IE|=bits;return HAL_OK;}
HAL_StatusTypeDef HAL_FDCAN_Start(FDCAN_HandleTypeDef *h) {assert(!fake_isr);++start_calls;h->State=HAL_FDCAN_STATE_BUSY;h->Instance->CCCR&=~FDCAN_CCCR_INIT;if(clear_bo_on_start)h->Instance->PSR&=~FDCAN_PSR_BO;return HAL_OK;}
HAL_StatusTypeDef HAL_FDCAN_Stop(FDCAN_HandleTypeDef *h) {assert(!fake_isr);++stop_calls;if(fail_stop){h->State=HAL_FDCAN_STATE_ERROR;return HAL_ERROR;}h->State=HAL_FDCAN_STATE_READY;h->Instance->CCCR|=FDCAN_CCCR_INIT;return HAL_OK;}
HAL_StatusTypeDef HAL_FDCAN_AbortTxRequest(FDCAN_HandleTypeDef *h,uint32_t bits) {assert(!fake_isr);++abort_calls;aborted_mask=bits;if(abort_ack)h->Instance->TXBRP&=~bits;return HAL_OK;}
uint32_t HAL_FDCAN_GetTxFifoFreeLevel(FDCAN_HandleTypeDef *h) {return h->Instance->TXFQS;}
HAL_StatusTypeDef HAL_FDCAN_AddMessageToTxFifoQ(FDCAN_HandleTypeDef *h,FDCAN_TxHeaderTypeDef *header,uint8_t *data) {
 if(!h->Instance->TXFQS)return HAL_ERROR;--h->Instance->TXFQS;++tx_calls;captured_header=*header;
 const uint8_t length=SRNOS_CANFDDlcToLength((uint8_t)header->DataLength);assert(length<=64u);
 memset(captured_data,0xa5,sizeof(captured_data));
 /* Match pinned HAL's complete-word payload reads, including partial words. */
 memcpy(captured_data,data,(length+3u)&~3u);return HAL_OK;
}
uint32_t HAL_FDCAN_GetRxFifoFillLevel(FDCAN_HandleTypeDef *h,uint32_t fifo) {(void)h;(void)fifo;return rx_remaining;}
HAL_StatusTypeDef HAL_FDCAN_GetRxMessage(FDCAN_HandleTypeDef *h,uint32_t fifo,FDCAN_RxHeaderTypeDef *header,uint8_t *data) {
 (void)h;(void)fifo;++rx_calls;if(fail_rx)return HAL_ERROR;assert(rx_remaining);if(!refill_rx)--rx_remaining;
 *header=custom_rx?custom_rx_header:(FDCAN_RxHeaderTypeDef){.Identifier=rx_sequence,.DataLength=8};
 const uint8_t length=SRNOS_CANFDDlcToLength((uint8_t)header->DataLength);assert(length<=64u);
 for(unsigned i=0;i<length;i++)data[i]=(uint8_t)(rx_sequence+(custom_rx?i:0u));++rx_sequence;return HAL_OK;
}
HAL_StatusTypeDef HAL_FDCAN_GetTxEvent(FDCAN_HandleTypeDef *h,FDCAN_TxEventFifoTypeDef *event) {(void)event;++tx_event_calls;assert(h->Instance->TXEFS);--h->Instance->TXEFS;return HAL_OK;}
