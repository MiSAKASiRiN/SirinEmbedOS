#include "IO/i2c_board.h"
#include "IO/i2c_timing.h"
#include "i2c.h"
#include "SirinEmbedOS/IO/I2C/I2C.h"
I2C_HandleTypeDef *SRNOS_BoardI2CHandle(uint8_t bus)
{
    return bus == SRNOS_I2C_BUS_4 ? &hi2c4 : NULL;
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

/* No runtime speed switching: apply once while the initialized bus is idle.
 * Refuse a changed PLL rather than silently driving an unintended bus speed. */
bool SRNOS_BoardI2CApplyTiming(I2C_HandleTypeDef *i2c)
{
    /* This bundled HAL's generic GetPeriphCLKFreq has no I2C4 branch. */
    PLL3_ClocksTypeDef pll3;
    HAL_RCCEx_GetPLL3ClockFreq(&pll3);
    if(!i2c || i2c->Instance!=I2C4 || HAL_I2C_GetState(i2c)!=HAL_I2C_STATE_READY ||
       __HAL_RCC_GET_I2C4_SOURCE()!=RCC_I2C4CLKSOURCE_PLL3 ||
       __HAL_RCC_GET_FLAG(RCC_FLAG_PLL3RDY)==RESET || pll3.PLL3_R_Frequency!=60000000U ||
       (i2c->Instance->CR1 & (I2C_CR1_ANFOFF|I2C_CR1_DNF))!=0U ||
       (i2c->Instance->ISR & I2C_ISR_BUSY)!=0U) return false;
    __HAL_I2C_DISABLE(i2c);
    i2c->Init.Timing=SRNOS_BOARD_I2C4_TIMING;
    WRITE_REG(i2c->Instance->TIMINGR,SRNOS_BOARD_I2C4_TIMING);
    __HAL_I2C_ENABLE(i2c);
    return true;
}
