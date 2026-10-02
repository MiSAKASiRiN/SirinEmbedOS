#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
BaseType_t xPortIsInsideInterrupt(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
const char *pcTaskGetName(TaskHandle_t);
