#pragma once
#include <stdint.h>
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
#define pdTRUE 1
#define pdFALSE 0
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
#define taskENTER_CRITICAL_FROM_ISR() 0u
#define taskEXIT_CRITICAL_FROM_ISR(x) ((void)(x))
#define portYIELD_FROM_ISR(x) ((void)(x))
