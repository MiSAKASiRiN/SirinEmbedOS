#pragma once
#include <stdint.h>
typedef void *osThreadId_t;
typedef void *osMutexId_t;
#define osWaitForever UINT32_MAX
enum { osKernelRunning=2, osOK=0 };
osMutexId_t osMutexNew(const void *);
int osMutexAcquire(osMutexId_t,uint32_t);
int osMutexRelease(osMutexId_t);
int osKernelGetState(void);
uint32_t osKernelGetTickFreq(void);
int osThreadYield(void);
int osDelay(uint32_t);

osThreadId_t osThreadGetId(void);
