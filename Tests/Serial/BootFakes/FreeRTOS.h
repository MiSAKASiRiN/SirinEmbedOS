#pragma once
#include <stdint.h>
#define taskSCHEDULER_NOT_STARTED 0
int xTaskGetSchedulerState(void);
void test_task_enter(void);
void test_task_exit(void);
#define taskENTER_CRITICAL() test_task_enter()
#define taskEXIT_CRITICAL() test_task_exit()
