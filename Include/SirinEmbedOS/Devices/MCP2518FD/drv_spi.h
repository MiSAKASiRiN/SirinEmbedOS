//
// drv_spi.h — BSP SPI shim for MCP2518FD
//
#ifndef LIB_MCP2518FD_DRV_SPI_H
#define LIB_MCP2518FD_DRV_SPI_H

#include <stdint.h>
#include "SirinEmbedOS/IO/SPI/bsp_spi.h"
#ifdef __cplusplus
extern "C" {
#endif

#ifndef DRV_SPI_MAX_DEVICES
#define DRV_SPI_MAX_DEVICES       (4u)
#endif

#ifndef SPI_DEFAULT_BUFFER_LENGTH
#define SPI_DEFAULT_BUFFER_LENGTH   (256u)
#endif

#ifndef _DRV_CANFDSPI_API_H
typedef uint8_t CANFDSPI_MODULE_ID;
#endif

void DRV_SPI_GetStatistics(CANFDSPI_MODULE_ID index, bsp_spi_statistics_t *out);
int8_t DRV_SPI_AttachDevice(CANFDSPI_MODULE_ID index,
                            bsp_spi_bus_id_t bus_id,
                            GPIO_TypeDef *cs_port,
                            uint16_t cs_pin);

int8_t DRV_SPI_TransferData(CANFDSPI_MODULE_ID index,
                            uint8_t *spiTxData,
                            uint8_t *spiRxData,
                            uint16_t spiTransferSize);

#ifdef __cplusplus
}
#endif

#endif // LIB_MCP2518FD_DRV_SPI_H
