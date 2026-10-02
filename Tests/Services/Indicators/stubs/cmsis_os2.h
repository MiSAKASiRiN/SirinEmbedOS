#pragma once
#include <stdint.h>
void osDelay(uint32_t);

typedef void *osThreadId_t;
#define osFlagsWaitAny 0u
osThreadId_t osThreadGetId(void);
uint32_t osThreadFlagsSet(osThreadId_t,uint32_t);
uint32_t osThreadFlagsWait(uint32_t,uint32_t,uint32_t);
