#pragma once
#include <stdint.h>
typedef void *osThreadId_t;
enum { osKernelRunning = 2 };
int osKernelGetState(void);
osThreadId_t osThreadGetId(void);
void osDelay(uint32_t);
