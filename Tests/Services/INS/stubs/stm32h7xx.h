#pragma once
#include <stdint.h>
uint32_t test_get_mask(void);
void test_disable_irq(void);
void test_set_mask(uint32_t);
#define __get_PRIMASK test_get_mask
#define __disable_irq test_disable_irq
#define __set_PRIMASK test_set_mask

extern uint32_t test_basepri;
#define __get_BASEPRI() test_basepri
