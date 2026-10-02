#pragma once
#include "stm32h7xx_hal.h"
/* Fixed board wiring; private to SDK implementations. */
#ifdef SRNOS_WS2812_HW_GPIO_PORT
#error "SRNOS_WS2812_HW_GPIO_PORT is fixed by Board/Support; application overrides are unsupported"
#endif
#define SRNOS_WS2812_HW_GPIO_PORT   GPIOA        // TIM1_CH3 default pin = PA10 AF1

#ifdef SRNOS_WS2812_HW_GPIO_PIN
#error "SRNOS_WS2812_HW_GPIO_PIN is fixed by Board/Support; application overrides are unsupported"
#endif
#define SRNOS_WS2812_HW_GPIO_PIN    10u

#ifdef SRNOS_WS2812_HW_GPIO_AF
#error "SRNOS_WS2812_HW_GPIO_AF is fixed by Board/Support; application overrides are unsupported"
#endif
#define SRNOS_WS2812_HW_GPIO_AF     1u          // AF1 for TIM1 on PA10

#ifdef SRNOS_WS2812_HW_TIM
#error "SRNOS_WS2812_HW_TIM is fixed by Board/Support; application overrides are unsupported"
#endif
#define SRNOS_WS2812_HW_TIM         TIM1         // Use TIM1 by default (advanced timer)

#ifdef SRNOS_WS2812_HW_DMA_STREAM
#error "SRNOS_WS2812_HW_DMA_STREAM is fixed by Board/Support; application overrides are unsupported"
#endif
#define SRNOS_WS2812_HW_DMA_STREAM DMA1_Stream1

#ifdef SRNOS_WS2812_HW_DMA_REQUEST
#error "SRNOS_WS2812_HW_DMA_REQUEST is fixed by Board/Support; application overrides are unsupported"
#endif
#define SRNOS_WS2812_HW_DMA_REQUEST DMA_REQUEST_TIM1_CH3

#ifdef SRNOS_WS2812_HW_DMA_DIER_FLAG
#error "SRNOS_WS2812_HW_DMA_DIER_FLAG is fixed by Board/Support; application overrides are unsupported"
#endif
#define SRNOS_WS2812_HW_DMA_DIER_FLAG TIM_DIER_CC3DE
