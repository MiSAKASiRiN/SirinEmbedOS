#pragma once
#include <stdint.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY } HAL_StatusTypeDef;
typedef struct { uint32_t APB2CLKDivider; } RCC_ClkInitTypeDef;
#define RCC_APB2_DIV1 1u
#define __NOP() ((void)0)
void HAL_RCC_GetClockConfig(RCC_ClkInitTypeDef *,uint32_t *);
uint32_t HAL_RCC_GetPCLK2Freq(void);
uint32_t HAL_GetTick(void);
