#ifndef TEST_FREERTOS_H
#define TEST_FREERTOS_H

#include <assert.h>
#include <stdint.h>

typedef uint32_t TickType_t;
typedef int BaseType_t;

#define pdTRUE             1
#define pdFALSE            0
#define pdMS_TO_TICKS(ms)  ((TickType_t)(ms))
#define configASSERT(expr) assert(expr)

void test_task_enter_critical(void);
void test_task_exit_critical(void);

#define taskENTER_CRITICAL() test_task_enter_critical()
#define taskEXIT_CRITICAL()  test_task_exit_critical()

#endif /* TEST_FREERTOS_H */
