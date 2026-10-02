#pragma once
#include <stdint.h>
extern uint16_t fake_cal[3];
#define VREFINT_CAL_ADDR (&fake_cal[0])
#define TEMPSENSOR_CAL1_ADDR (&fake_cal[1])
#define TEMPSENSOR_CAL2_ADDR (&fake_cal[2])
