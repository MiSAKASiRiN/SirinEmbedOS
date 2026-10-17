#pragma once
#include <stdint.h>
typedef struct { unsigned capacity, size, head, count; uint8_t *storage; } StaticQueue_t;
typedef StaticQueue_t *QueueHandle_t;
QueueHandle_t xQueueCreateStatic(unsigned count, unsigned size, uint8_t *storage, StaticQueue_t *control);
int xQueueSend(QueueHandle_t queue, const void *item, unsigned wait);
int xQueueReceive(QueueHandle_t queue, void *item, unsigned wait);
int xQueuePeek(QueueHandle_t queue, void *item, unsigned wait);
int xQueueOverwrite(QueueHandle_t queue, const void *item);
unsigned uxQueueMessagesWaiting(QueueHandle_t queue);
