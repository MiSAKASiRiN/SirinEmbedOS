#pragma once
#include <stdint.h>
template<class Step,class Wait> static inline void SRNOS_KernelRunCpp(Step step,Wait wait,uint32_t=0,bool wait_first=true)
{if(!wait_first)step();for(;;){if(!wait())return;step();}}
