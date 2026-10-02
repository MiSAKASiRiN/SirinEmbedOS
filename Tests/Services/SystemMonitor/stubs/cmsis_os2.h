#pragma once
#include <stdint.h>
typedef void *osThreadId_t;
typedef int osStatus_t;
typedef enum {osKernelInactive,osKernelReady,osKernelRunning} osKernelState_t;
typedef enum {osPriorityLow=8} osPriority_t;
typedef struct {const char *name; uint32_t stack_size; osPriority_t priority;} osThreadAttr_t;
#define osFlagsWaitAny 0u
osKernelState_t osKernelGetState(void);
osThreadId_t osThreadNew(void (*)(void *),void *,const osThreadAttr_t *);
osThreadId_t osThreadGetId(void);
uint32_t osKernelGetTickFreq(void);
osStatus_t osDelay(uint32_t);
uint32_t osThreadFlagsWait(uint32_t,uint32_t,uint32_t);
uint32_t osThreadFlagsClear(uint32_t);
uint32_t osThreadFlagsSet(osThreadId_t,uint32_t);
