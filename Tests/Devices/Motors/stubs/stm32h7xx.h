#pragma once
#include <cstdint>
using IRQn_Type = int;
uint32_t __get_BASEPRI();
void __set_BASEPRI(uint32_t value);
void __set_BASEPRI_MAX(uint32_t value);
uint32_t __get_IPSR();
uint32_t NVIC_GetPriority(IRQn_Type);
void __DMB();
void __DSB();
void __ISB();
void SRNOS_TestMotorException(uint32_t exception, uint32_t priority);
