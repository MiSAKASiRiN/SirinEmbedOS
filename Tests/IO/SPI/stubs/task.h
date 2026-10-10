#pragma once
#include "cmsis_os2.h"
typedef void *TaskHandle_t;
typedef int BaseType_t;
typedef uint32_t TickType_t;
#define pdFALSE 0
#define eSetBits 1
#define portYIELD_FROM_ISR(x) ((void)(x))
static inline BaseType_t xTaskNotify(TaskHandle_t t,uint32_t f,int a) { (void)a;return (BaseType_t)osThreadFlagsSet(t,f); }
static inline BaseType_t xTaskNotifyFromISR(TaskHandle_t t,uint32_t f,int a,BaseType_t *w) { *w=0;return xTaskNotify(t,f,a); }
static inline BaseType_t xTaskNotifyWait(uint32_t a,uint32_t b,uint32_t *v,TickType_t ticks) { (void)a;*v=osThreadFlagsWait(b,osFlagsWaitAny,ticks);return *v!=0; }
