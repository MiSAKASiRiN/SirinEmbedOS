#pragma once
#include <stdint.h>
extern uint32_t fake_ipsr;
static inline uint32_t __get_IPSR(void) {return fake_ipsr;}
#define __DMB() ((void)0)
#define __DSB() ((void)0)
