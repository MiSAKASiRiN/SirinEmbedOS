#include "parameters_internal.h"
#if defined(__arm__) || defined(__thumb__)
#include "stm32h7xx.h"
namespace SRNOS::Parameters::detail {
bool TaskContext() { return __get_IPSR() == 0U && __get_PRIMASK() == 0U && __get_BASEPRI() == 0U; }
uint32_t LockValues() { const uint32_t state = __get_PRIMASK(); __disable_irq(); __DMB(); return state; }
void UnlockValues(uint32_t state) { __DMB(); __set_PRIMASK(state); }
}
#else
#include <mutex>
namespace SRNOS::Parameters::detail {
static std::recursive_mutex value_lock;
bool TaskContext() { return true; }
uint32_t LockValues() { value_lock.lock(); return 0; }
void UnlockValues(uint32_t) { value_lock.unlock(); }
}
#endif
