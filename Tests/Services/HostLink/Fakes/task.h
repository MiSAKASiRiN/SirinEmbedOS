#ifndef TEST_TASK_H
#define TEST_TASK_H

#include "FreeRTOS.h"

#include <stdint.h>

typedef void *TaskHandle_t;

TaskHandle_t xTaskGetCurrentTaskHandle(void);
uint32_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
BaseType_t xTaskNotifyWait(uint32_t clear_on_entry,
                           uint32_t clear_on_exit,
                           uint32_t *value,
                           TickType_t timeout);
void xTaskNotifyGive(TaskHandle_t task);

#endif /* TEST_TASK_H */
