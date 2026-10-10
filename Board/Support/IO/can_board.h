#ifndef SRNOS_CAN_BOARD_H
#define SRNOS_CAN_BOARD_H
#include "IO/SPI/spi_internal.h"
#include "SirinEmbedOS/IO/CAN/Controller.h"
typedef struct
{
    uint8_t          dev_id;
    SRNOS_SPIBusId bus_id;
    GPIO_TypeDef    *cs_port;
    uint16_t         cs_pin;
    uint16_t         int_pin;
    uint16_t         int0_pin;
    uint16_t         int1_pin;
} SRNOS_ExternalCANDevCfg;
#ifdef __cplusplus
extern "C" {
#endif
const SRNOS_ExternalCANDevCfg *SRNOS_BoardExternalCANConfig(uint8_t dev);
FDCAN_HandleTypeDef *SRNOS_BoardCANHandle(uint8_t bus);
/* Startup only, before any native controller starts. Does not enable IRQs or
 * start the controller; failure must leave the caller's start gate closed. */
bool SRNOS_BoardCANApplyConfig(uint8_t bus, const SRNOS_CANControllerConfig *config);
#ifdef __cplusplus
}
#endif
#endif
