#pragma once
#include "SirinEmbedOS/Diagnostics/Runtime/TaskStatistics.h"
#include <string.h>
static inline void SRNOS_CycleSample(uint8_t slot,uint32_t gen,SRNOS_MonitorCycle *out)
{memset(out,0,sizeof(*out));out->slot=slot;out->generation=gen;}

static inline uint32_t SRNOS_CycleMaxCriticalCycles(void){return 0;}
