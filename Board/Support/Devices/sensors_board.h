#pragma once
#include "IO/SPI/spi_internal.h"
#include "SirinEmbedOS/IO/I2C/I2C.h"
#include "Devices/IMU/BMI088/bmi088_internal.h"
void SRNOS_BoardSensorBindings(SRNOS_SPIDevice *,SRNOS_I2CDevice *,SRNOS_I2CDevice *);
SRNOS_BMI088Status SRNOS_BoardBMIInit(SRNOS_BMI088CompletionFn completion, void *user);
