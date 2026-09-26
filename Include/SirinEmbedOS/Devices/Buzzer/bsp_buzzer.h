#ifndef __BSP_BUZZER_H
#define __BSP_BUZZER_H

#pragma once

#include "stm32h7xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#ifndef BUZZER_GPIO_PORT
  #define BUZZER_GPIO_PORT      GPIOE
#endif
#ifndef BUZZER_PIN
  #define BUZZER_PIN            13u
#endif
#ifndef BUZZER_GPIO_CLK_EN
  #define BUZZER_GPIO_CLK_EN()  __HAL_RCC_GPIOE_CLK_ENABLE()
#endif

#ifndef BUZZER_TIM
  #define BUZZER_TIM            TIM2
#endif
#ifndef BUZZER_TIM_CLK_EN
  #define BUZZER_TIM_CLK_EN()   __HAL_RCC_TIM2_CLK_ENABLE()
#endif
#ifndef BUZZER_TIM_IRQn
  #define BUZZER_TIM_IRQn       TIM2_IRQn
#endif

#ifndef BUZZER_DMA
  #define BUZZER_DMA            DMA1_Stream0
#endif
#ifndef BUZZER_DMA_REQ
  #define BUZZER_DMA_REQ        DMA_REQUEST_TIM2_UP
#endif

#ifndef BUZZER_QUEUE_CAPACITY
  #define BUZZER_QUEUE_CAPACITY 16
#endif

#if BUZZER_QUEUE_CAPACITY < 2
  #error "BUZZER_QUEUE_CAPACITY must be at least 2 because one ring slot is reserved"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUZZER_STATUS_OK = 0,
    BUZZER_STATUS_NOT_INIT,
    BUZZER_STATUS_INVALID_ARG,
    BUZZER_STATUS_QUEUE_FULL,
    BUZZER_STATUS_HW_ERROR,
} BuzzerStatus;

typedef uint32_t (*BuzzerNowMsHook)(void *ctx);
typedef uint32_t (*BuzzerEnterHook)(void *ctx);
typedef void (*BuzzerExitHook)(void *ctx, uint32_t token);

typedef struct {
    BuzzerNowMsHook now_ms;
    BuzzerEnterHook enter;
    BuzzerExitHook exit;
    BuzzerEnterHook enter_from_isr;
    BuzzerExitHook exit_from_isr;
} Buzzer_PortHooks;

typedef struct {
    Buzzer_PortHooks hooks;
    void *hook_ctx;
} Buzzer_Config;

typedef struct {
    uint32_t freq_hz;   /* 0 = rest (silence) */
    uint16_t dur_ms;    /* duration in milliseconds */
} BuzzerNote;

BuzzerStatus Buzzer_Init(const Buzzer_Config *cfg);

BuzzerStatus Buzzer_Start(uint32_t freq_hz);
BuzzerStatus Buzzer_Stop(void);
BuzzerStatus Buzzer_SetFrequency(uint32_t freq_hz);
BuzzerStatus Buzzer_SetRefA4(float hz);
BuzzerStatus Buzzer_StartNote(const char *note);

BuzzerStatus Buzzer_Enqueue(BuzzerNote n);
BuzzerStatus Buzzer_EnqueueMany(const BuzzerNote *arr, uint16_t n);
BuzzerStatus Buzzer_Flush(void);

BuzzerStatus Buzzer_EnqueueFromISR(BuzzerNote n);
BuzzerStatus Buzzer_FlushFromISR(void);

BuzzerStatus Buzzer_Service(void);

bool Buzzer_IsActive(void);
bool Buzzer_QueueEmpty(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_BUZZER_H */
