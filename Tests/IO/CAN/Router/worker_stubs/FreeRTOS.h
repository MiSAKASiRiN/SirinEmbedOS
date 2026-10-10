#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
#define pdFALSE 0
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define configTICK_RATE_HZ 1000u
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define portYIELD_FROM_ISR(higher) ((void)(higher))
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
