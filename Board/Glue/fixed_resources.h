#ifndef SRNOS_FIXED_RESOURCES_H
#define SRNOS_FIXED_RESOURCES_H
#include "EXFDCAN_Router_Internal.h"
#ifdef __cplusplus
extern "C" {
#endif
const exfdcan_dev_cfg_t *SRNOS_BoardExternalCANConfig(uint8_t dev);
FDCAN_HandleTypeDef *SRNOS_BoardCANHandle(uint8_t bus);
#ifdef __cplusplus
}
#endif
#endif
