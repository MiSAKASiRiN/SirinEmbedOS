//
// drv_spi.h — BSP SPI shim for MCP2518FD
//
#ifndef SRNOS_MCP2518FD_SPI_ADAPTER_INTERNAL_H
#define SRNOS_MCP2518FD_SPI_ADAPTER_INTERNAL_H

#include <stdint.h>
#include "IO/SPI/spi_internal.h"
#ifdef __cplusplus
extern "C" {
#endif

#ifndef SRNOS_MCP2518FD_MAX_DEVICES
#define SRNOS_MCP2518FD_MAX_DEVICES       (4u)
#endif

#ifndef SPI_DEFAULT_BUFFER_LENGTH
#define SPI_DEFAULT_BUFFER_LENGTH   (256u)
#endif

#ifndef _DRV_CANFDSPI_API_H
typedef uint8_t CANFDSPI_MODULE_ID;
#endif

void SRNOS_MCP2518FDGetSPIStatistics(CANFDSPI_MODULE_ID index, SRNOS_SPIStatistics *out);
int8_t SRNOS_MCP2518FDAttachSPI(CANFDSPI_MODULE_ID index,
                            SRNOS_SPIBusId bus_id,
                            GPIO_TypeDef *cs_port,
                            uint16_t cs_pin);

int8_t DRV_SPI_TransferData(CANFDSPI_MODULE_ID index,
                            uint8_t *spiTxData,
                            uint8_t *spiRxData,
                            uint16_t spiTransferSize);

#ifdef __cplusplus
}
#endif

#endif // SRNOS_MCP2518FD_SPI_ADAPTER_INTERNAL_H
