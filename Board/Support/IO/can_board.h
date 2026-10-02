#ifndef SRNOS_CAN_BOARD_H
#define SRNOS_CAN_BOARD_H
#include "IO/SPI/spi_internal.h"
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
#ifdef __cplusplus
}
#endif
#endif
