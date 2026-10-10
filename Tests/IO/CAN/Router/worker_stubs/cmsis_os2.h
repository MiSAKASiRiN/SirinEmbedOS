#pragma once
#include <stdint.h>
typedef void *osEventFlagsId_t;
typedef void *osThreadId_t;
#define osFlagsWaitAny 0u
#define osWaitForever UINT32_MAX
#define osFlagsError 0x80000000u
osEventFlagsId_t osEventFlagsNew(const void *);
uint32_t osEventFlagsSet(osEventFlagsId_t,uint32_t);
uint32_t osEventFlagsWait(osEventFlagsId_t,uint32_t,uint32_t,uint32_t);
int osThreadYield(void);
