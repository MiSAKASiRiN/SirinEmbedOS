#pragma once
#include "stm32h7xx_hal.h"
/* Fixed board wiring; private to SDK implementations. */
#ifdef SRNOS_WS2812_GPIO_PORT
#error "SRNOS_WS2812_GPIO_PORT is fixed by Board/Support; application overrides are unsupported"
#endif
#define SRNOS_WS2812_GPIO_PORT      GPIOA

#ifdef SRNOS_WS2812_GPIO_PIN
#error "SRNOS_WS2812_GPIO_PIN is fixed by Board/Support; application overrides are unsupported"
#endif
#define SRNOS_WS2812_GPIO_PIN       GPIO_PIN_10

#ifdef SRNOS_WS2812_GPIO_AF
#error "SRNOS_WS2812_GPIO_AF is fixed by Board/Support; application overrides are unsupported"
#endif
#define SRNOS_WS2812_GPIO_AF        GPIO_AF1_TIM1

#ifdef SRNOS_WS2812_TIM
#error "SRNOS_WS2812_TIM is fixed by Board/Support; application overrides are unsupported"
#endif
#define SRNOS_WS2812_TIM            TIM1

#ifdef SRNOS_WS2812_DMA_STREAM
#error "SRNOS_WS2812_DMA_STREAM is fixed by Board/Support; application overrides are unsupported"
#endif
#define SRNOS_WS2812_DMA_STREAM     DMA1_Stream1

#ifdef SRNOS_WS2812_DMA_REQUEST
#error "SRNOS_WS2812_DMA_REQUEST is fixed by Board/Support; application overrides are unsupported"
#endif
#define SRNOS_WS2812_DMA_REQUEST    DMA_REQUEST_TIM1_UP

#ifdef SRNOS_WS2812DMAIRQn
#error "SRNOS_WS2812DMAIRQn is fixed by Board/Support; application overrides are unsupported"
#endif
#define SRNOS_WS2812DMAIRQn       DMA1_Stream1_IRQn

#ifdef SRNOS_WS2812_DMA_IRQ_PRIO
#error "SRNOS_WS2812_DMA_IRQ_PRIO is fixed by Board/Support; application overrides are unsupported"
#endif
#define SRNOS_WS2812_DMA_IRQ_PRIO   9
