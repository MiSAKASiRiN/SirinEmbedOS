#pragma once
#include <stdint.h>
typedef int IRQn_Type;
extern _Thread_local uint32_t test_irq,test_irq_priority;
static inline uint32_t __get_IPSR(void){return test_irq;}
static inline uint32_t NVIC_GetPriority(IRQn_Type irq){(void)irq;return test_irq_priority;}
