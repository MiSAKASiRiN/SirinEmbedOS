#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
extern int test_isr;
static inline int xPortIsInsideInterrupt(void) {return test_isr;}
static inline void vTaskNotifyGiveFromISR(TaskHandle_t t,BaseType_t *w) {(void)t;(void)w;}
static inline void xTaskNotifyGive(TaskHandle_t t) {(void)t;}
#define eSetBits 0
static inline int xTaskNotifyFromISR(TaskHandle_t t,uint32_t b,int a,BaseType_t *w) {(void)t;(void)b;(void)a;(void)w;return 1;}
