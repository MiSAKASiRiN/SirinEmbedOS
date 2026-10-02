#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef enum { SRNOS_WS2812_OK,SRNOS_WS2812_ERROR,SRNOS_WS2812_BUSY,SRNOS_WS2812_TIMEOUT } SRNOS_WS2812Result;
void SRNOS_WS2812Init(uint32_t);
void SRNOS_WS2812SetAll(uint8_t,uint8_t,uint8_t);
void SRNOS_WS2812SetPixel(uint16_t,uint8_t,uint8_t,uint8_t);
SRNOS_WS2812Result SRNOS_WS2812CommitStartAsync(const void *);
uint8_t SRNOS_WS2812IsBusy(void);
void SRNOS_WS2812DMAIRQHandler(void);
