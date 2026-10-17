#pragma once
#include "SirinEmbedOS/Kernel/Thread.h"
#include <stddef.h>
typedef void *TaskHandle_t;
typedef void *osThreadId_t;
typedef void (*osThreadFunc_t)(void *);
typedef struct { const char *name; unsigned stack_size, priority; } osThreadAttr_t;
enum { osPriorityBelowNormal = 16 };
typedef int BaseType_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
#define portYIELD_FROM_ISR(x) ((void)(x))
typedef struct { void (*step)(void *); bool (*wait)(void *); void *context; uint32_t period_ms; bool wait_first; } SRNOS_InternalLoop;
void SRNOS_KernelRunLoop(const SRNOS_InternalLoop *loop);
bool SRNOS_KernelTaskContext(void);
osThreadId_t SRNOS_KernelThreadNew(osThreadFunc_t entry, void *arg, const osThreadAttr_t *attr);
void xTaskNotifyGive(TaskHandle_t task);
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *wake);
uint32_t ulTaskNotifyTake(BaseType_t clear, uint32_t timeout);
void vTaskDelay(uint32_t ticks);
