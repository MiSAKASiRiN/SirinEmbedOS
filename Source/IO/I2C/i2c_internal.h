#pragma once
#include "stm32h7xx_hal.h"
#include "SirinEmbedOS/IO/I2C/I2C.h"
#ifdef __cplusplus
extern "C" {
#endif
void SRNOS_I2CInit(void);
void SRNOS_I2CInitRtos(void);
#ifdef __cplusplus
}
#endif
