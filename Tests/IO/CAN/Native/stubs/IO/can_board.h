#pragma once
#include "stm32h7xx_hal.h"
#include "SirinEmbedOS/IO/CAN/Controller.h"
#ifdef __cplusplus
extern "C" {
#endif
FDCAN_HandleTypeDef *SRNOS_BoardCANHandle(uint8_t);
bool SRNOS_BoardCANApplyConfig(uint8_t, const SRNOS_CANControllerConfig *);
#ifdef __cplusplus
}
#endif
