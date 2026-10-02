#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "cmsis_os2.h"
#define SRNOS_BOARD_ADC_EVENT 1u
typedef struct { uint16_t raw[5]; uint64_t start_us, end_us; bool error; } SRNOS_BoardADCResult;
/* Sole caller/owner is SystemMonitor. All except HAL callbacks are task-only. */
bool SRNOS_BoardADCInitialize(osThreadId_t owner,uint16_t calibration[3]);
bool SRNOS_BoardADCStart(void);
bool SRNOS_BoardADCTake(SRNOS_BoardADCResult *out);
bool SRNOS_BoardADCStop(void);
