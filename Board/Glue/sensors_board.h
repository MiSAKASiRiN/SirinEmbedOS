#pragma once
#include "SirinEmbedOS/IO/SPI/bsp_spi.h"
#include "SirinEmbedOS/IO/I2C/bsp_i2c.h"
#include "SirinEmbedOS/Devices/IMU/BMI088/bsp_bmi088.h"
void SRNOS_BoardSensorBindings(bsp_spi_device_t *,bsp_i2c_device_t *,bsp_i2c_device_t *);
bsp_bmi088_status_t SRNOS_BoardBMIInit(bsp_bmi088_completion_fn completion, void *user);
