#pragma once
#include <stdint.h>
typedef void *osMutexId_t;typedef void *osMessageQueueId_t;typedef void *osThreadId_t;
typedef enum {osKernelInactive,osKernelReady,osKernelRunning} osKernelState_t;
typedef enum {osOK=0,osError=-1} osStatus_t;
typedef struct {const char *name;uint32_t attr_bits;void *cb_mem;uint32_t cb_size;} osMutexAttr_t;
typedef struct {const char *name;uint32_t attr_bits;void *cb_mem;uint32_t cb_size;void *stack_mem;uint32_t stack_size;int priority;uint32_t tz_module,reserved;} osThreadAttr_t;
#define osWaitForever UINT32_MAX
#define osMutexRecursive 1u
#define osMutexPrioInherit 2u
#define osPriorityAboveNormal 32
#define osFlagsWaitAny 0u
osKernelState_t osKernelGetState(void);
uint32_t osKernelGetTickCount(void);uint32_t osKernelGetTickFreq(void);
osMutexId_t osMutexNew(const osMutexAttr_t *);
osStatus_t osMutexAcquire(osMutexId_t,uint32_t);osStatus_t osMutexRelease(osMutexId_t);
osMessageQueueId_t osMessageQueueNew(uint32_t,uint32_t,const void *);
osStatus_t osMessageQueuePut(osMessageQueueId_t,const void *,uint8_t,uint32_t);
osStatus_t osMessageQueueGet(osMessageQueueId_t,void *,uint8_t *,uint32_t);
osThreadId_t osThreadNew(void (*)(void*),void *,const osThreadAttr_t *);
uint32_t osThreadFlagsSet(osThreadId_t,uint32_t);uint32_t osThreadFlagsWait(uint32_t,uint32_t,uint32_t);
osStatus_t osDelay(uint32_t);
