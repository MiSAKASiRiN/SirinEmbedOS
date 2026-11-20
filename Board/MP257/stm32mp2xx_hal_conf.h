#pragma once
#define HAL_MODULE_ENABLED
#define HAL_FDCAN_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HSE_VALUE 40000000U
#define HSI_VALUE 64000000U
#define MSI_VALUE 4000000U
#define EXTERNAL_CLOCK_VALUE 12288000U
#define LSE_VALUE 32768U
#define LSI_VALUE 32000U
#define assert_param(expr) ((void)0)
#include "stm32mp2xx_hal_fdcan.h"
#include "stm32mp2xx_hal_gpio.h"
#include "stm32mp2xx_hal_rcc.h"
