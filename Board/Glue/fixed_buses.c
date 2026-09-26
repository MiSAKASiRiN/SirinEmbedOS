#include "fixed_buses.h"
#include "spi.h"
#include "i2c.h"
#include "SirinEmbedOS/IO/SPI/bsp_spi.h"
#include "SirinEmbedOS/IO/I2C/bsp_i2c.h"
extern DMA_HandleTypeDef hdma_spi4_tx, hdma_spi4_rx;
void SRNOS_BoardSPIHandles(uint8_t bus, SPI_HandleTypeDef **spi, DMA_HandleTypeDef **tx, DMA_HandleTypeDef **rx)
{
    *spi = bus == BSP_SPI_BUS_4 ? &hspi4 : NULL;
    *tx = bus == BSP_SPI_BUS_4 ? &hdma_spi4_tx : NULL;
    *rx = bus == BSP_SPI_BUS_4 ? &hdma_spi4_rx : NULL;
}
I2C_HandleTypeDef *SRNOS_BoardI2CHandle(uint8_t bus)
{
    return bus == BSP_I2C_BUS_4 ? &hi2c4 : NULL;
}
