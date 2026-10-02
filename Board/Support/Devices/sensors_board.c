#include "Devices/sensors_board.h"
#include "main.h"
void SRNOS_BoardSensorBindings(SRNOS_SPIDevice *lsm,SRNOS_I2CDevice *lis,SRNOS_I2CDevice *spa)
{
    (void)SRNOS_SPIDeviceInit(lsm,SRNOS_SPI_BUS_4,LSM_CS_GPIO_Port,LSM_CS_Pin);
    lsm->imu_priority=1;
    lsm->mode=3;
    (void)SRNOS_I2CDeviceInit(lis,SRNOS_I2C_BUS_4,0x1e);
    (void)SRNOS_I2CDeviceInit(spa,SRNOS_I2C_BUS_4,0x76);
}
SRNOS_BMI088Status SRNOS_BoardBMIInit(SRNOS_BMI088CompletionFn completion, void *user)
{
    return SRNOS_BMI088Init(SRNOS_SPI_BUS_4,BMI_ACC_CS_GPIO_Port,BMI_ACC_CS_Pin,
                          BMI_CS_GYRO_GPIO_Port,BMI_CS_GYRO_Pin,completion,user);
}
