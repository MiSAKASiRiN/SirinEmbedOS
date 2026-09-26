#pragma once
#include "stm32h7xx_hal.h"
#ifdef __cplusplus
extern "C" {
#endif
void SRNOS_BoardSPIHandles(uint8_t bus, SPI_HandleTypeDef **spi, DMA_HandleTypeDef **tx, DMA_HandleTypeDef **rx);
I2C_HandleTypeDef *SRNOS_BoardI2CHandle(uint8_t bus);
#ifdef __cplusplus
}
#endif
