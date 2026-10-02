#pragma once
#include "stm32h7xx.h"
#include <stdint.h>
typedef struct {uint32_t CR,ISR;} ADC_TypeDef;
typedef struct {uint32_t CCR;} BDMA_Channel_TypeDef;
typedef struct {void *Instance;uint32_t interrupts,flags;} DMA_HandleTypeDef;
typedef struct {ADC_TypeDef *Instance;DMA_HandleTypeDef *DMA_Handle;uint32_t ErrorCode;} ADC_HandleTypeDef;
extern ADC_TypeDef fake_adc;
extern BDMA_Channel_TypeDef fake_dma;
extern ADC_HandleTypeDef hadc3;
#define ADC3 (&fake_adc)
#define BDMA_Channel2 (&fake_dma)
#define ADC_CR_ADSTART 1u
#define ADC_CR_ADEN 2u
#define BDMA_CCR_EN 1u
#define ADC_FLAG_OVR 1u
#define ADC_CALIB_OFFSET 0u
#define ADC_SINGLE_ENDED 0u
#define HAL_ADC_ERROR_NONE 0u
#define DMA_IT_HT 1u
#define DMA_IT_TC 2u
#define DMA_IT_TE 4u
#define BDMA_Channel2_IRQn 2
void test_dma_disable(DMA_HandleTypeDef *);
#define __HAL_DMA_DISABLE(h) test_dma_disable(h)
#define __HAL_DMA_DISABLE_IT(h,b) ((h)->interrupts&=~(b))
#define __HAL_DMA_CLEAR_FLAG(h,b) ((h)->flags&=~(b))
#define __HAL_BDMA_GET_GI_FLAG_INDEX(h) 15u
#define __HAL_ADC_GET_FLAG(h,b) ((h)->Instance->ISR&(b))
typedef enum {HAL_OK,HAL_ERROR} HAL_StatusTypeDef;
HAL_StatusTypeDef HAL_ADC_Stop_DMA(ADC_HandleTypeDef *);
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *,uint32_t *,uint32_t);
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *,uint32_t,uint32_t);
void HAL_NVIC_DisableIRQ(int);
void HAL_NVIC_EnableIRQ(int);
void HAL_NVIC_ClearPendingIRQ(int);
