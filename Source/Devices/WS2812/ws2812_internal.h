#pragma once
#include "stm32h7xx_hal.h"
#include "SirinEmbedOS/Devices/WS2812/WS2812.h"
#ifdef __cplusplus
extern "C" {
#endif
void SRNOS_WS2812Init(uint32_t tim_clk_hz);
void SRNOS_WS2812DMAIRQHandler(void);
uint16_t *SRNOS_WS2812AcquireDmaScratch(uint32_t min_words, uint32_t *capacity_words);
void SRNOS_WS2812WaitIdle(void);
#ifdef __cplusplus
}
#endif
