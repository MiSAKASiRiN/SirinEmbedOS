#pragma once
#include "task.h"
typedef TaskHandle_t osThreadId_t;typedef void (*osThreadFunc_t)(void *);
typedef enum {osKernelInactive,osKernelReady,osKernelRunning} osKernelState_t;
typedef int osPriority_t;
typedef struct {const char *name;uint32_t attr_bits;void *cb_mem;uint32_t cb_size;void *stack_mem;uint32_t stack_size;osPriority_t priority;uint32_t tz_module,reserved;} osThreadAttr_t;
osKernelState_t osKernelGetState(void);
osThreadId_t osThreadNew(osThreadFunc_t,void *,const osThreadAttr_t *);
