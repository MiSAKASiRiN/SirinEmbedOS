#pragma once
#include "stm32h7xx_hal.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
bool SRNOS_BoardI2CQuiescent(I2C_HandleTypeDef *);
I2C_HandleTypeDef *SRNOS_BoardI2CHandle(uint8_t bus);
#ifdef __cplusplus
}
#endif
