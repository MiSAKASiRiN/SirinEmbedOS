#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
typedef enum { eNoAction=0,eSetBits } eNotifyAction;
#ifdef __cplusplus
extern "C" {
#endif
TaskHandle_t xTaskGetCurrentTaskHandle(void);
BaseType_t xTaskNotify(TaskHandle_t,uint32_t,eNotifyAction);
BaseType_t xTaskNotifyFromISR(TaskHandle_t,uint32_t,eNotifyAction,BaseType_t *);
BaseType_t xTaskNotifyWait(uint32_t,uint32_t,uint32_t *,TickType_t);
#ifdef __cplusplus
}
#endif
