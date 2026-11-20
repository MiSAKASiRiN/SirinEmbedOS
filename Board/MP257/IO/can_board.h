#pragma once
#include "stm32mp2xx_hal.h"
#include "SirinEmbedOS/IO/CAN/Controller.h"
#ifdef __cplusplus
extern "C" {
#endif
FDCAN_HandleTypeDef *SRNOS_BoardCANHandle(uint8_t bus);
bool SRNOS_BoardCANApplyConfig(uint8_t bus, const SRNOS_CANControllerConfig *config);
void SRNOS_BoardShutdownEnable(void);
#ifdef __cplusplus
}
#endif
