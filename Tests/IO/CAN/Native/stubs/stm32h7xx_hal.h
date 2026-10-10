#pragma once
#include <stdint.h>
#include <stddef.h>
#define __IO volatile
#define USE_HAL_FDCAN_REGISTER_CALLBACKS 0
#define HAL_FDCAN_ERROR_NONE 0u
#define FDCAN_IT_RX_FIFO0_NEW_MESSAGE (1u<<0)
#define FDCAN_IT_RX_FIFO0_MESSAGE_LOST (1u<<1)
#define FDCAN_IT_RX_FIFO1_NEW_MESSAGE (1u<<2)
#define FDCAN_IT_RX_FIFO1_MESSAGE_LOST (1u<<3)
#define FDCAN_IT_TX_EVT_FIFO_NEW_DATA (1u<<4)
#define FDCAN_IT_BUS_OFF (1u<<7)
#define FDCAN_IT_RAM_WATCHDOG (1u<<8)
#define FDCAN_IT_ERROR_PASSIVE (1u<<9)
#define FDCAN_IR_BO FDCAN_IT_BUS_OFF
#define FDCAN_IR_EP FDCAN_IT_ERROR_PASSIVE
#define FDCAN_IR_EW (1u<<10)
#define FDCAN_PSR_BO (1u<<7)
#define FDCAN_PSR_LEC_Msk 7u
#define FDCAN_PSR_LEC_Pos 0u
#define FDCAN_CCCR_INIT 1u
#define FDCAN_TXEFS_EFFL_Msk 0x3fu
#define FDCAN_TXFQS_TFFL_Msk 0x3fu
#define FDCAN_TXFQS_TFFL_Pos 0u
#define FDCAN_FILTER_MASK 0u
#define FDCAN_FILTER_DISABLE 0u
#define FDCAN_STANDARD_ID 0u
#define FDCAN_EXTENDED_ID 1u
#define FDCAN_ACCEPT_IN_RX_FIFO0 0u
#define FDCAN_FILTER_REMOTE 1u
#define FDCAN_RX_FIFO0 0u
#define FDCAN_RX_FIFO1 1u
#define FDCAN_FILTER_TO_RXFIFO0 0u
#define FDCAN_FILTER_TO_RXFIFO1 1u
#define FDCAN_DLC_BYTES_0 0u
#define FDCAN_DLC_BYTES_1 1u
#define FDCAN_DLC_BYTES_2 2u
#define FDCAN_DLC_BYTES_3 3u
#define FDCAN_DLC_BYTES_4 4u
#define FDCAN_DLC_BYTES_5 5u
#define FDCAN_DLC_BYTES_6 6u
#define FDCAN_DLC_BYTES_7 7u
#define FDCAN_DLC_BYTES_8 8u
#define FDCAN_DATA_FRAME 0u
#define FDCAN_REMOTE_FRAME 1u
#define FDCAN_ESI_ACTIVE 0u
#define FDCAN_ESI_PASSIVE 1u
#define FDCAN_BRS_OFF 0u
#define FDCAN_BRS_ON 1u
#define FDCAN_CLASSIC_CAN 0u
#define FDCAN_FD_CAN 1u
#define FDCAN_FRAME_CLASSIC 0u
#define FDCAN_FRAME_FD_NO_BRS 1u
#define FDCAN_FRAME_FD_BRS 2u
#define FDCAN_DATA_BYTES_8 4u
#define FDCAN_DATA_BYTES_64 18u
#define FDCAN_TX_FIFO_OPERATION 0u
#define RCC_PERIPHCLK_FDCAN 1u
#define FDCANCCU_CCFG_CDIV (0xfu<<16)
#define FDCAN_STORE_TX_EVENTS 1u
#define __HAL_FDCAN_CLEAR_FLAG(h,f) ((h)->Instance->IR &= ~(f))
typedef enum {HAL_OK=0,HAL_ERROR=1} HAL_StatusTypeDef;
typedef enum {HAL_FDCAN_STATE_READY,HAL_FDCAN_STATE_BUSY,HAL_FDCAN_STATE_ERROR} HAL_FDCAN_StateTypeDef;
typedef struct {uint32_t TXFQS,PSR,CCCR,TXBRP,TXEFS,IR,IE;} FDCAN_GlobalTypeDef;
typedef struct {
 uint32_t FrameFormat,NominalPrescaler,NominalTimeSeg1,NominalTimeSeg2,NominalSyncJumpWidth;
 uint32_t DataPrescaler,DataTimeSeg1,DataTimeSeg2,DataSyncJumpWidth;
 uint32_t MessageRAMOffset,StdFiltersNbr,ExtFiltersNbr;
 uint32_t RxFifo0ElmtsNbr,RxFifo0ElmtSize,RxFifo1ElmtsNbr,RxFifo1ElmtSize,RxBuffersNbr,RxBufferSize;
 uint32_t TxBuffersNbr,TxFifoQueueElmtsNbr,TxEventsNbr,TxFifoQueueMode,TxElmtSize;
} FDCAN_InitTypeDef;
typedef struct {FDCAN_GlobalTypeDef *Instance; HAL_FDCAN_StateTypeDef State; uint32_t ErrorCode; FDCAN_InitTypeDef Init;} FDCAN_HandleTypeDef;
typedef struct {uint32_t Identifier,DataLength,IdType,RxFrameType,FDFormat,BitRateSwitch,ErrorStateIndicator;} FDCAN_RxHeaderTypeDef;
typedef struct {uint32_t CCFG;} FDCAN_ClockCalibrationUnit_TypeDef;
typedef struct {uint32_t Identifier,DataLength,IdType,TxFrameType,ErrorStateIndicator,BitRateSwitch,FDFormat,TxEventFifoControl,MessageMarker;} FDCAN_TxHeaderTypeDef;
typedef struct {uint32_t FilterType,FilterConfig,IdType,FilterIndex,FilterID1,FilterID2;} FDCAN_FilterTypeDef;
typedef struct {uint32_t marker;} FDCAN_TxEventFifoTypeDef;
#ifdef __cplusplus
extern "C" {
#endif
extern FDCAN_GlobalTypeDef fake_registers[3];
extern FDCAN_ClockCalibrationUnit_TypeDef fake_ccu;
#define FDCAN_CCU (&fake_ccu)
#define FDCAN1 (&fake_registers[0])
#define FDCAN2 (&fake_registers[1])
#define FDCAN3 (&fake_registers[2])
uint32_t HAL_GetTick(void);
uint32_t __get_IPSR(void);
uint32_t HAL_RCCEx_GetPeriphCLKFreq(uint32_t);
HAL_StatusTypeDef HAL_FDCAN_Init(FDCAN_HandleTypeDef *);
HAL_StatusTypeDef HAL_FDCAN_ConfigTxDelayCompensation(FDCAN_HandleTypeDef *,uint32_t,uint32_t);
HAL_StatusTypeDef HAL_FDCAN_EnableTxDelayCompensation(FDCAN_HandleTypeDef *);
HAL_StatusTypeDef HAL_FDCAN_DisableTxDelayCompensation(FDCAN_HandleTypeDef *);
HAL_StatusTypeDef HAL_FDCAN_ConfigFilter(FDCAN_HandleTypeDef *,FDCAN_FilterTypeDef *);
HAL_StatusTypeDef HAL_FDCAN_ConfigGlobalFilter(FDCAN_HandleTypeDef *,uint32_t,uint32_t,uint32_t,uint32_t);
HAL_StatusTypeDef HAL_FDCAN_ActivateNotification(FDCAN_HandleTypeDef *,uint32_t,uint32_t);
HAL_StatusTypeDef HAL_FDCAN_Start(FDCAN_HandleTypeDef *);
HAL_StatusTypeDef HAL_FDCAN_Stop(FDCAN_HandleTypeDef *);
HAL_StatusTypeDef HAL_FDCAN_AbortTxRequest(FDCAN_HandleTypeDef *,uint32_t);
uint32_t HAL_FDCAN_GetTxFifoFreeLevel(FDCAN_HandleTypeDef *);
HAL_StatusTypeDef HAL_FDCAN_AddMessageToTxFifoQ(FDCAN_HandleTypeDef *,FDCAN_TxHeaderTypeDef *,uint8_t *);
uint32_t HAL_FDCAN_GetRxFifoFillLevel(FDCAN_HandleTypeDef *,uint32_t);
HAL_StatusTypeDef HAL_FDCAN_GetRxMessage(FDCAN_HandleTypeDef *,uint32_t,FDCAN_RxHeaderTypeDef *,uint8_t *);
HAL_StatusTypeDef HAL_FDCAN_GetTxEvent(FDCAN_HandleTypeDef *,FDCAN_TxEventFifoTypeDef *);
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *,uint32_t);
void HAL_FDCAN_RxFifo1Callback(FDCAN_HandleTypeDef *,uint32_t);
void HAL_FDCAN_TxEventFifoCallback(FDCAN_HandleTypeDef *,uint32_t);
void HAL_FDCAN_TxBufferCompleteCallback(FDCAN_HandleTypeDef *,uint32_t);
void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *,uint32_t);
void HAL_FDCAN_ErrorCallback(FDCAN_HandleTypeDef *);
void HAL_FDCAN_IRQHandler(FDCAN_HandleTypeDef *);
#ifdef __cplusplus
}
#endif
