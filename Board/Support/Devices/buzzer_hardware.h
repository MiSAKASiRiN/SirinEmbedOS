#pragma once
#include "stm32h7xx_hal.h"
/* Fixed board wiring; private to SDK implementations. */
#ifdef SRNOS_BUZZER_GPIO_PORT
#error "SRNOS_BUZZER_GPIO_PORT is fixed by Board/Support; application overrides are unsupported"
#endif
#define SRNOS_BUZZER_GPIO_PORT      GPIOE

#ifdef SRNOS_BUZZER_PIN
#error "SRNOS_BUZZER_PIN is fixed by Board/Support; application overrides are unsupported"
#endif
#define SRNOS_BUZZER_PIN            13u

#ifdef SRNOS_BUZZER_GPIO_CLK_EN
#error "SRNOS_BUZZER_GPIO_CLK_EN is fixed by Board/Support; application overrides are unsupported"
#endif
#define SRNOS_BUZZER_GPIO_CLK_EN()  __HAL_RCC_GPIOE_CLK_ENABLE()

#ifdef SRNOS_BUZZER_TIM
#error "SRNOS_BUZZER_TIM is fixed by Board/Support; application overrides are unsupported"
#endif
#define SRNOS_BUZZER_TIM            TIM2

#ifdef SRNOS_BUZZER_TIM_CLK_EN
#error "SRNOS_BUZZER_TIM_CLK_EN is fixed by Board/Support; application overrides are unsupported"
#endif
#define SRNOS_BUZZER_TIM_CLK_EN()   __HAL_RCC_TIM2_CLK_ENABLE()

#ifdef SRNOS_BUZZER_TimIRQn
#error "SRNOS_BUZZER_TimIRQn is fixed by Board/Support; application overrides are unsupported"
#endif
#define SRNOS_BUZZER_TimIRQn       TIM2_IRQn

#ifdef SRNOS_BUZZER_DMA
#error "SRNOS_BUZZER_DMA is fixed by Board/Support; application overrides are unsupported"
#endif
#define SRNOS_BUZZER_DMA            DMA1_Stream0

#ifdef SRNOS_BUZZER_DMA_REQ
#error "SRNOS_BUZZER_DMA_REQ is fixed by Board/Support; application overrides are unsupported"
#endif
#define SRNOS_BUZZER_DMA_REQ        DMA_REQUEST_TIM2_UP
