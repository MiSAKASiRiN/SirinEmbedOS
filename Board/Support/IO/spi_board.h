#pragma once
#include "stm32h7xx_hal.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
void SRNOS_BoardSPIHandles(uint8_t bus, SPI_HandleTypeDef **spi, DMA_HandleTypeDef **tx, DMA_HandleTypeDef **rx);
bool SRNOS_BoardSPISelectMode(SPI_HandleTypeDef *, uint8_t mode);
bool SRNOS_BoardSPIAbort(SPI_HandleTypeDef *, DMA_HandleTypeDef *, DMA_HandleTypeDef *);
#ifdef __cplusplus
}
#endif
