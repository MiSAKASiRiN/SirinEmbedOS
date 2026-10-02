#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Internal service: prepare once before scheduler; worker owned by Kernel. */
bool SRNOS_IndicatorsPrepare(bool bringup);
void SRNOS_IndicatorsStep(uint32_t now);
void SRNOS_IndicatorsWorker(void *argument);
