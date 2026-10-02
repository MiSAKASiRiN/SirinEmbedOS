#pragma once
#include "stm32h7xx_hal.h"
#include <stdbool.h>

/* Fixed PLL3R = 60 MHz, analog filter ON (50..80 ns), DNF=0.
 * PRESC=1, SCLDEL=12, SDADEL=7, SCLH=24, SCLL=45.
 * Fast mode: 0..300 ns rise/fall allowance; zero-edge fastest SCL <400 kHz.
 * Keep OMNICTL2_3.ioc I2C4.Timing in sync. See Tools/Debug/check_i2c_timing.py. */
#define SRNOS_BOARD_I2C4_TIMING 0x10C7182DU
/* Startup only, after CubeMX HAL/filter initialization, before any transfers.
 * USER CODE hook validates the clock/filter contract and reapplies IOC timing. */
bool SRNOS_BoardI2CApplyTiming(I2C_HandleTypeDef *);
