#pragma once
#include "stm32h7xx_hal.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
void SRNOS_BoardSPIHandles(uint8_t bus, SPI_HandleTypeDef **spi, DMA_HandleTypeDef **tx, DMA_HandleTypeDef **rx);
bool SRNOS_BoardSPISelectMode(SPI_HandleTypeDef *, uint8_t mode);
bool SRNOS_BoardI2CQuiescent(I2C_HandleTypeDef *);
bool SRNOS_BoardSPIAbort(SPI_HandleTypeDef *, DMA_HandleTypeDef *, DMA_HandleTypeDef *);
I2C_HandleTypeDef *SRNOS_BoardI2CHandle(uint8_t bus);
#ifdef __cplusplus
}
#endif
