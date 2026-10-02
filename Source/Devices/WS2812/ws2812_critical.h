//
// Created by sirin on 2025/10/4.
//

#ifndef SRNOS_WS2812_CRITICAL_H
#define SRNOS_WS2812_CRITICAL_H

#pragma once
#include <stdint.h>
#include "stm32h7xx.h"

#ifdef __cplusplus
extern "C" {
#endif

    // ===================== Protocol timing configuration =====================

#ifndef SRNOS_WS2812_CRITICAL_LED_COUNT
#define SRNOS_WS2812_CRITICAL_LED_COUNT   3u
#endif

    // WS2812 timings (ns); hardware binding is private to Board/Support.
#ifndef SRNOS_WS2812_CRITICAL_T0H_NS
#define SRNOS_WS2812_CRITICAL_T0H_NS      350u
#endif
#ifndef SRNOS_WS2812_CRITICAL_T1H_NS
#define SRNOS_WS2812_CRITICAL_T1H_NS      700u
#endif
#ifndef SRNOS_WS2812_CRITICAL_BITRATE_HZ
#define SRNOS_WS2812_CRITICAL_BITRATE_HZ  800000u
#endif
#ifndef SRNOS_WS2812_CRITICAL_RESET_SLOTS
#define SRNOS_WS2812_CRITICAL_RESET_SLOTS 80u
#endif
#ifndef SRNOS_WS2812_CRITICAL_RESET_US
#define SRNOS_WS2812_CRITICAL_RESET_US    80u
#endif
#ifndef SRNOS_WS2812_CRITICAL_WAIT_SPIN_LIMIT
#define SRNOS_WS2812_CRITICAL_WAIT_SPIN_LIMIT 1000000u
#endif
#ifndef SRNOS_WS2812_CRITICAL_DMA_TIMEOUT_SPINS
#define SRNOS_WS2812_CRITICAL_DMA_TIMEOUT_SPINS 10000000u
#endif

    void SRNOS_WS2812CriticalInit(uint32_t tim_clk_hz);
    void SRNOS_WS2812CriticalSet(uint16_t index, uint8_t r, uint8_t g, uint8_t b); // GRB order
    void SRNOS_WS2812CriticalFill(uint8_t r, uint8_t g, uint8_t b);
    void SRNOS_WS2812CriticalTransmit(void);  // blocking, no IRQ

#ifdef __cplusplus
}
#endif

#endif //SRNOS_WS2812_CRITICAL_H
