#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY } HAL_StatusTypeDef;
typedef struct { uint32_t APB2CLKDivider; } RCC_ClkInitTypeDef;
#define RCC_APB2_DIV1 1u
#define __NOP() ((void)0)
void HAL_RCC_GetClockConfig(RCC_ClkInitTypeDef *,uint32_t *);
uint32_t HAL_RCC_GetPCLK2Freq(void);
uint32_t HAL_GetTick(void);
void WS2812_Init(uint32_t);
void WS2812_SetAll(uint8_t,uint8_t,uint8_t);
void WS2812_SetPixel(uint16_t,uint8_t,uint8_t,uint8_t);
HAL_StatusTypeDef WS2812_Commit_StartAsync(const void *);
uint8_t WS2812_IsBusy(void);
void WS2812_DMA_IRQHandler(void);
