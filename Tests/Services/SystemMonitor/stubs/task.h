#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
typedef enum {eRunning,eReady,eBlocked,eSuspended,eDeleted,eInvalid} eTaskState;
typedef struct { uint64_t ulRunTimeCounter; eTaskState eCurrentState; UBaseType_t uxCurrentPriority,uxBasePriority; } TaskStatus_t;
UBaseType_t uxTaskGetTaskNumber(TaskHandle_t);
void vTaskSetTaskNumber(TaskHandle_t,UBaseType_t);
const char *pcTaskGetName(TaskHandle_t);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
TaskHandle_t xTaskGetIdleTaskHandle(void);
void vTaskGetInfo(TaskHandle_t,TaskStatus_t *,BaseType_t,eTaskState);
