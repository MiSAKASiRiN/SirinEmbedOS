#pragma once
#include "cmsis_os2.h"
#include "stm32h7xx.h"
extern osKernelState_t test_kernel_state;
extern bool test_fail_create,test_manual_clock;
extern uint64_t test_cycles;
extern uint32_t test_identity_generation,test_yields;
void test_runtime_init(void);
