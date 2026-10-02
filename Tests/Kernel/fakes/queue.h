#pragma once
#include "task.h"
typedef StaticQueue_t *QueueHandle_t;
QueueHandle_t xQueueCreateStatic(unsigned,unsigned,uint8_t *,StaticQueue_t *);
BaseType_t xQueueSend(QueueHandle_t,const void *,TickType_t);
BaseType_t xQueueReceive(QueueHandle_t,void *,TickType_t);
BaseType_t xQueueSendFromISR(QueueHandle_t,const void *,BaseType_t *);
BaseType_t xQueueReceiveFromISR(QueueHandle_t,void *,BaseType_t *);
void vQueueDelete(QueueHandle_t);
