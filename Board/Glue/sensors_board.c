#include "sensors_board.h"
#include "main.h"
void SRNOS_BoardSensorBindings(bsp_spi_device_t *lsm,bsp_i2c_device_t *lis,bsp_i2c_device_t *spa)
{
    (void)bsp_spi_device_init(lsm,BSP_SPI_BUS_4,LSM_CS_GPIO_Port,LSM_CS_Pin);
    lsm->imu_priority=1;
    lsm->mode=3;
    (void)bsp_i2c_device_init(lis,BSP_I2C_BUS_4,0x1e);
    (void)bsp_i2c_device_init(spa,BSP_I2C_BUS_4,0x76);
}
bsp_bmi088_status_t SRNOS_BoardBMIInit(bsp_bmi088_completion_fn completion, void *user)
{
    return BSP_BMI088_Init(BSP_SPI_BUS_4,BMI_ACC_CS_GPIO_Port,BMI_ACC_CS_Pin,
                          BMI_CS_GYRO_GPIO_Port,BMI_CS_GYRO_Pin,completion,user);
}
