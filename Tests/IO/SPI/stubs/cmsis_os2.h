#pragma once
#include <stdint.h>
typedef void *osThreadId_t;
typedef void *osSemaphoreId_t;
typedef struct { const char *name; uint32_t stack_size; int priority; } osThreadAttr_t;
enum { osKernelRunning=2, osPriorityRealtime1=49, osOK=0, osFlagsWaitAny=0 };
#define osWaitForever UINT32_MAX
uint32_t osKernelGetTickFreq(void);
int osKernelGetState(void);
osThreadId_t osThreadGetId(void);
osThreadId_t osThreadNew(void (*)(void*),void*,const osThreadAttr_t*);
uint32_t osThreadFlagsSet(osThreadId_t,uint32_t);
uint32_t osThreadFlagsClear(uint32_t);
uint32_t osThreadFlagsWait(uint32_t,uint32_t,uint32_t);
osSemaphoreId_t osSemaphoreNew(uint32_t,uint32_t,const void*);
int osSemaphoreAcquire(osSemaphoreId_t,uint32_t);
int osSemaphoreRelease(osSemaphoreId_t);
