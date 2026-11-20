#pragma once

#include "FreeRTOS.h"
#include "device_internal.h"
#include <cstdint>

namespace SRNOS::Devices::Detail {

/* Fixed-board serialization for RX, timeout updates and snapshot copies.
 * No scheduler dependency, allocation, spin or blocking; preserve a stronger
 * caller mask. Priority 0..4 handlers must not access these motor objects. */
class StateGuard {
public:
    StateGuard() : saved_(__get_BASEPRI()) {
        const uint32_t exception = __get_IPSR();
        configASSERT(exception == 0u ||
                     (exception >= 16u &&
                      NVIC_GetPriority(static_cast<IRQn_Type>(exception - 16u)) >=
                          configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY));
        __set_BASEPRI_MAX(configMAX_SYSCALL_INTERRUPT_PRIORITY);
        __DSB();
        __ISB();
    }
    ~StateGuard() {
        __DMB();
        __set_BASEPRI(saved_);
    }
    StateGuard(const StateGuard&) = delete;
    StateGuard& operator=(const StateGuard&) = delete;
private:
    uint32_t saved_;
};

inline bool feedbackExpired(uint32_t now_ms, uint32_t last_ms, uint32_t timeout_ms) {
    const uint32_t age = now_ms - last_ms;
    /* A TX tick sampled before a newer RX publication is not a 49-day gap.
     * The shared uint32_t clock supports forward intervals below half-range. */
    return age <= INT32_MAX && age > timeout_ms;
}

} // namespace SRNOS::Devices::Detail
