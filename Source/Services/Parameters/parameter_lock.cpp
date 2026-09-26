#include "SirinEmbedOS/Services/Parameters/lib_params.hpp"
#if defined(__arm__) || defined(__thumb__)
#include "stm32h7xx.h"
namespace omx::params::detail {
uint32_t LockValues() { const uint32_t state = __get_PRIMASK(); __disable_irq(); __DMB(); return state; }
void UnlockValues(uint32_t state) { __DMB(); __set_PRIMASK(state); }
}
#else
#include <mutex>
namespace omx::params::detail {
static std::recursive_mutex value_lock;
uint32_t LockValues() { value_lock.lock(); return 0; }
void UnlockValues(uint32_t) { value_lock.unlock(); }
}
#endif
