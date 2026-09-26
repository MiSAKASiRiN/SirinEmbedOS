//
// Created by sirin on 2025/10/4.
//

#ifndef H723VG_V2_FREERTOS_WS2812_CRITICAL_H
#define H723VG_V2_FREERTOS_WS2812_CRITICAL_H

#pragma once
#include <stdint.h>
#include "stm32h7xx.h"

#ifdef __cplusplus
extern "C" {
#endif

    // ===================== User Config (override before include) =====================
#ifndef WS2812CT_GPIO_PORT
#define WS2812CT_GPIO_PORT   GPIOA        // TIM1_CH3 default pin = PA10 AF1
#endif
#ifndef WS2812CT_GPIO_PIN
#define WS2812CT_GPIO_PIN    10u
#endif
#ifndef WS2812CT_GPIO_AF
#define WS2812CT_GPIO_AF     1u          // AF1 for TIM1 on PA10
#endif
#ifndef WS2812CT_LED_COUNT
#define WS2812CT_LED_COUNT   3u
#endif
#ifndef WS2812CT_TIM
#define WS2812CT_TIM         TIM1         // Use TIM1 by default (advanced timer)
#endif

    // WS2812 timings (ns)
#ifndef WS2812CT_T0H_NS
#define WS2812CT_T0H_NS      350u
#endif
#ifndef WS2812CT_T1H_NS
#define WS2812CT_T1H_NS      700u
#endif
#ifndef WS2812CT_BITRATE_HZ
#define WS2812CT_BITRATE_HZ  800000u
#endif
#ifndef WS2812CT_RESET_SLOTS
#define WS2812CT_RESET_SLOTS 80u
#endif
#ifndef WS2812CT_RESET_US
#define WS2812CT_RESET_US    80u
#endif
#ifndef WS2812CT_WAIT_SPIN_LIMIT
#define WS2812CT_WAIT_SPIN_LIMIT 1000000u
#endif
#ifndef WS2812CT_DMA_TIMEOUT_SPINS
#define WS2812CT_DMA_TIMEOUT_SPINS 10000000u
#endif
#ifndef WS2812CT_DMA_STREAM
#define WS2812CT_DMA_STREAM DMA1_Stream1
#endif
#ifndef WS2812CT_DMA_REQUEST
#define WS2812CT_DMA_REQUEST DMA_REQUEST_TIM1_CH3
#endif
#ifndef WS2812CT_DMA_DIER_FLAG
#define WS2812CT_DMA_DIER_FLAG TIM_DIER_CC3DE
#endif

    void ws2812ct_init(uint32_t tim_clk_hz);
    void ws2812ct_set(uint16_t index, uint8_t r, uint8_t g, uint8_t b); // GRB order
    void ws2812ct_fill(uint8_t r, uint8_t g, uint8_t b);
    void ws2812ct_tx_blocking(void);  // blocking, no IRQ

#ifdef __cplusplus
}
#endif

#endif //H723VG_V2_FREERTOS_WS2812_CRITICAL_H
