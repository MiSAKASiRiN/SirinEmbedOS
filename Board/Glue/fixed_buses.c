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

/* Blocking HAL abort clears peripheral/DMA flags. Fence NVIC leftovers before
 * dispatcher may assign the shared staging buffer to another device. */
bool SRNOS_BoardSPIAbort(SPI_HandleTypeDef *spi, DMA_HandleTypeDef *tx, DMA_HandleTypeDef *rx)
{
    HAL_StatusTypeDef st=HAL_SPI_Abort(spi);
    bool stopped=st==HAL_OK && HAL_SPI_GetState(spi)==HAL_SPI_STATE_READY &&
        HAL_DMA_GetState(tx)!=HAL_DMA_STATE_BUSY && HAL_DMA_GetState(rx)!=HAL_DMA_STATE_BUSY;
    if(stopped) {
        HAL_NVIC_ClearPendingIRQ(SPI4_IRQn);
        HAL_NVIC_ClearPendingIRQ(DMA2_Stream0_IRQn);
        HAL_NVIC_ClearPendingIRQ(DMA2_Stream1_IRQn);
        __DSB(); __ISB();
    }
    return stopped;
}

/* Immutable per-device mode, applied with all CS high between transactions.
 * LSM6DS3TR-C datasheet specifies idle-high / sample on rising edge (mode 3).
 * BMI and MCP retain their proven mode 0; clock stays at 10 MHz. */
bool SRNOS_BoardSPISelectMode(SPI_HandleTypeDef *spi, uint8_t mode)
{
    if(mode!=0 && mode!=3) return false;
    if(HAL_SPI_GetState(spi)!=HAL_SPI_STATE_READY) return false;
    uint32_t bits=mode==3?(SPI_CFG2_CPOL|SPI_CFG2_CPHA):0;
    if((spi->Instance->CFG2 & (SPI_CFG2_CPOL|SPI_CFG2_CPHA|SPI_CFG2_AFCNTR))!=(bits|SPI_CFG2_AFCNTR)) {
        __HAL_SPI_DISABLE(spi);
        MODIFY_REG(spi->Instance->CFG2,SPI_CFG2_CPOL|SPI_CFG2_CPHA|SPI_CFG2_AFCNTR,bits|SPI_CFG2_AFCNTR);
        spi->Init.CLKPolarity=mode==3?SPI_POLARITY_HIGH:SPI_POLARITY_LOW;
        spi->Init.CLKPhase=mode==3?SPI_PHASE_2EDGE:SPI_PHASE_1EDGE;
        /* Establish idle SCK BEFORE CS falls. AFCNTR retains that level
         * while SPE is disabled for the next DMA setup. */
        __HAL_SPI_ENABLE(spi);
        __DSB();
        __HAL_SPI_DISABLE(spi);
        spi->Init.MasterKeepIOState=SPI_MASTER_KEEP_IO_STATE_ENABLE;
        __DSB();
    }
    return true;
}

bool SRNOS_BoardI2CQuiescent(I2C_HandleTypeDef *i2c)
{
    if(HAL_I2C_GetState(i2c)!=HAL_I2C_STATE_READY ||
       (i2c->hdmatx && HAL_DMA_GetState(i2c->hdmatx)==HAL_DMA_STATE_BUSY) ||
       (i2c->hdmarx && HAL_DMA_GetState(i2c->hdmarx)==HAL_DMA_STATE_BUSY)) return false;
    HAL_NVIC_ClearPendingIRQ(I2C4_EV_IRQn); HAL_NVIC_ClearPendingIRQ(I2C4_ER_IRQn);
    HAL_NVIC_ClearPendingIRQ(BDMA_Channel0_IRQn); HAL_NVIC_ClearPendingIRQ(BDMA_Channel1_IRQn);
    __DSB(); __ISB(); return true;
}
