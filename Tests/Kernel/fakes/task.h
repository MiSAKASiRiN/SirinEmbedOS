#pragma once
#include "FreeRTOS.h"
typedef StaticTask_t *TaskHandle_t;
typedef enum {eNoAction,eSetBits} eNotifyAction;
TaskHandle_t xTaskGetCurrentTaskHandle(void);
TickType_t xTaskGetTickCount(void);TickType_t xTaskGetTickCountFromISR(void);
void vTaskDelay(TickType_t);void vTaskSuspend(TaskHandle_t);void vTaskDelete(TaskHandle_t);
BaseType_t xTaskNotifyIndexed(TaskHandle_t,unsigned,uint32_t,eNotifyAction);
BaseType_t xTaskNotifyIndexedFromISR(TaskHandle_t,unsigned,uint32_t,eNotifyAction,BaseType_t *);
BaseType_t xTaskNotifyWaitIndexed(unsigned,uint32_t,uint32_t,uint32_t *,TickType_t);
