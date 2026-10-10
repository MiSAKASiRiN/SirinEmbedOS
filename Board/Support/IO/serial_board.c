#include "serial_internal.h"
#include "SirinEmbedOS/Core/Memory/MemorySections.h"
#include "usart.h"
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
/* DMA1/2 reachable non-cacheable memory. All UART DMA is owned here. */
static uint8_t rx_dma[SRNOS_SERIAL_PORT_COUNT][SRNOS_SERIAL_RX_DMA_SIZE] SRNOS_DMA_NC_BUF_BSS;
static uint8_t tx_dma[SRNOS_SERIAL_PORT_COUNT][SRNOS_SERIAL_MAX_WRITE] SRNOS_DMA_NC_BUF_BSS;
static SRNOS_SerialDmaCursor cursor[SRNOS_SERIAL_PORT_COUNT];
static bool initialized[SRNOS_SERIAL_PORT_COUNT];
int SRNOS_SerialBoardIsInISR(void){return __get_IPSR()!=0;}
static UART_HandleTypeDef *handle(SRNOS_SerialPortId p) {
 switch(p){case SRNOS_SERIAL_USART1:return &huart1;case SRNOS_SERIAL_USART6:return &huart6;
 case SRNOS_SERIAL_UART7:return &huart7;case SRNOS_SERIAL_UART8:return &huart8;case SRNOS_SERIAL_UART9:return &huart9;default:return NULL;}
}
static SRNOS_SerialPortId port(UART_HandleTypeDef *h) {
 for(unsigned i=1;i<SRNOS_SERIAL_PORT_COUNT;i++)if(handle((SRNOS_SerialPortId)i)==h)return (SRNOS_SerialPortId)i;
 return SRNOS_SERIAL_NONE;
}
static void rx_callback(UART_HandleTypeDef *h,uint16_t size) {
 SRNOS_SerialPortId p=port(h);if(!p||!initialized[p])return;
 /* UART IDLE and DMA IRQs share priority 6; serialize their shared cursor. */
 UBaseType_t mask=taskENTER_CRITICAL_FROM_ISR();
 /* HAL emits HT, TC and IDLE positions. Keeping HT enabled disambiguates full laps. */
 (void)size;
 /* Read live NDTR: a delayed HT event must not replay bytes already seen at IDLE.
  * Track a wrap consumed by IDLE so the subsequent TC cannot deliver it twice. */
 size_t position=SRNOS_SERIAL_RX_DMA_SIZE-__HAL_DMA_GET_COUNTER(h->hdmarx);
 size_t before=cursor[p].position;
 size_t n=SRNOS_SerialDmaAdvance(&cursor[p],position,SRNOS_SERIAL_RX_DMA_SIZE,
 HAL_UARTEx_GetRxEventType(h)==HAL_UART_RXEVENT_TC);
 /* More than one physical ring means ISR latency exceeded the retention window. */
 if(n>SRNOS_SERIAL_RX_DMA_SIZE){taskEXIT_CRITICAL_FROM_ISR(mask);SRNOS_SerialOnError(p);return;}
 size_t first=n;if(first>SRNOS_SERIAL_RX_DMA_SIZE-before)first=SRNOS_SERIAL_RX_DMA_SIZE-before;
 if(first)SRNOS_SerialOnRx(p,&rx_dma[p][before],first);
 if(n>first)SRNOS_SerialOnRx(p,rx_dma[p],n-first);
 taskEXIT_CRITICAL_FROM_ISR(mask);
}
static void tx_callback(UART_HandleTypeDef *h) {
 /* HAL's non-circular TX DMA callback enables TCIE; this callback runs at UART TC. */
 if(port(h)==SRNOS_SERIAL_UART7&&SRNOS_SerialBoardConfig(SRNOS_SERIAL_UART7)->rx_enabled)SET_BIT(h->Instance->CR1,USART_CR1_RE);
 SRNOS_SerialOnTxComplete(port(h));
}
static void error_callback(UART_HandleTypeDef *h) {SRNOS_SerialOnError(port(h));}
static int begin_rx(SRNOS_SerialPortId p) {
 const SRNOS_SerialConfig *cfg=SRNOS_SerialBoardConfig(p);
 if(!cfg||!cfg->rx_enabled)return 0;
 cursor[p]=(SRNOS_SerialDmaCursor){0};
 return HAL_UARTEx_ReceiveToIdle_DMA(handle(p),rx_dma[p],SRNOS_SERIAL_RX_DMA_SIZE)==HAL_OK?0:-1;
}
int SRNOS_SerialBoardStart(const SRNOS_SerialConfig *c) {
 UART_HandleTypeDef *h=handle(c->port);if(!h||!h->hdmarx||!h->hdmatx)return -1;
 HAL_UART_Abort(h);
 h->Init.BaudRate=c->baud_rate;
 unsigned hardware_bits=c->data_bits+(c->parity!=SRNOS_SERIAL_PARITY_NONE);
 h->Init.WordLength=hardware_bits==9?UART_WORDLENGTH_9B:(hardware_bits==7?UART_WORDLENGTH_7B:UART_WORDLENGTH_8B);
 h->Init.StopBits=c->stop_bits==2?UART_STOPBITS_2:UART_STOPBITS_1;
 h->Init.Parity=c->parity==SRNOS_SERIAL_PARITY_EVEN?UART_PARITY_EVEN:(c->parity==SRNOS_SERIAL_PARITY_ODD?UART_PARITY_ODD:UART_PARITY_NONE);
 h->Init.Mode=(c->rx_enabled?UART_MODE_RX:0)|(c->tx_enabled?UART_MODE_TX:0);
 h->AdvancedInit.AdvFeatureInit=UART_ADVFEATURE_RXINVERT_INIT;
 h->AdvancedInit.RxPinLevelInvert=c->rx_inverted?UART_ADVFEATURE_RXINV_ENABLE:UART_ADVFEATURE_RXINV_DISABLE;
 HAL_StatusTypeDef s=c->mode==SRNOS_SERIAL_RS485?HAL_RS485Ex_Init(h,UART_DE_POLARITY_HIGH,0,0):HAL_UART_Init(h);
 if(s!=HAL_OK)return -1;
 h->hdmarx->Init.Mode=DMA_CIRCULAR;
 if(HAL_DMA_Init(h->hdmarx)!=HAL_OK)return -1;
 if(HAL_UART_RegisterRxEventCallback(h,rx_callback)!=HAL_OK||
 HAL_UART_RegisterCallback(h,HAL_UART_TX_COMPLETE_CB_ID,tx_callback)!=HAL_OK||
 HAL_UART_RegisterCallback(h,HAL_UART_ERROR_CB_ID,error_callback)!=HAL_OK)return -1;
 initialized[c->port]=true;return begin_rx(c->port);
}
int SRNOS_SerialBoardTransmit(SRNOS_SerialPortId p,const uint8_t *data,size_t n) {
 if(!initialized[p]||!n||n>SRNOS_SERIAL_MAX_WRITE)return -1;
 memcpy(tx_dma[p],data,n);__DMB();
 /* Suppress local RS485 echo while DE is asserted; re-enable only at TC. */
 if(p==SRNOS_SERIAL_UART7)CLEAR_BIT(handle(p)->Instance->CR1,USART_CR1_RE);
 if(HAL_UART_Transmit_DMA(handle(p),tx_dma[p],(uint16_t)n)!=HAL_OK){
  if(p==SRNOS_SERIAL_UART7&&SRNOS_SerialBoardConfig(p)->rx_enabled)SET_BIT(handle(p)->Instance->CR1,USART_CR1_RE);return -1;}
 return 0;
}
void SRNOS_SerialBoardAbortTx(SRNOS_SerialPortId p){HAL_UART_AbortTransmit(handle(p));if(p==SRNOS_SERIAL_UART7&&SRNOS_SerialBoardConfig(p)->rx_enabled)SET_BIT(handle(p)->Instance->CR1,USART_CR1_RE);}
int SRNOS_SerialBoardRecover(SRNOS_SerialPortId p) {
 const SRNOS_SerialConfig *c=SRNOS_SerialBoardConfig(p);
 if(!initialized[p])return c?SRNOS_SerialBoardStart(c):-1;
 initialized[p]=false;HAL_UART_AbortReceive(handle(p));initialized[p]=true;return begin_rx(p);
}
