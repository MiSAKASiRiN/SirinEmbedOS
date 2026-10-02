#pragma once
#include <stdint.h>
typedef void *osMessageQueueId_t;
typedef void *osMemoryPoolId_t;
typedef enum { osKernelInactive,osKernelReady,osKernelRunning,osKernelLocked } osKernelState_t;
typedef enum { osOK=0,osError=-1,osErrorTimeout=-2,osErrorResource=-3,
    osErrorParameter=-4,osErrorNoMemory=-5,osErrorISR=-6 } osStatus_t;
osKernelState_t osKernelGetState(void);
void *osMemoryPoolAlloc(osMemoryPoolId_t,uint32_t);
osStatus_t osMemoryPoolFree(osMemoryPoolId_t,void *);
osStatus_t osMessageQueuePut(osMessageQueueId_t,const void *,uint8_t,uint32_t);
osStatus_t osMessageQueueGet(osMessageQueueId_t,void *,uint8_t *,uint32_t);
