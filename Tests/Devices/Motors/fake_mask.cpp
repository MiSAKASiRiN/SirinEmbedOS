#include "stm32h7xx.h"
#include <atomic>
#include <mutex>

namespace {
std::mutex mask;
thread_local uint32_t basepri, ipsr, irq_priority = 5;
}
uint32_t __get_BASEPRI() { return basepri; }
void __set_BASEPRI(uint32_t value) {
    if (!basepri && value) mask.lock();
    const bool unlock = basepri && !value;
    basepri = value;
    if (unlock) mask.unlock();
}
void __set_BASEPRI_MAX(uint32_t value) {
    if (value && (!basepri || value < basepri)) __set_BASEPRI(value);
}
uint32_t __get_IPSR() { return ipsr; }
uint32_t NVIC_GetPriority(IRQn_Type) { return irq_priority; }
void __DMB() { std::atomic_signal_fence(std::memory_order_seq_cst); }
void __DSB() { __DMB(); }
void __ISB() { __DMB(); }
void SRNOS_TestMotorException(uint32_t exception, uint32_t priority) {
    ipsr = exception;
    irq_priority = priority;
}
