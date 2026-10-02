#pragma once
#include <assert.h>
#include <stdint.h>
typedef uint32_t UBaseType_t;
typedef int BaseType_t;
typedef uint32_t TickType_t;
#define configMAX_TASK_NAME_LEN 16
#define pdFALSE 0
#define pdTRUE 1
#define configASSERT assert
void test_enter(void);
void test_exit(void);
#define taskENTER_CRITICAL() test_enter()
#define taskEXIT_CRITICAL() test_exit()
