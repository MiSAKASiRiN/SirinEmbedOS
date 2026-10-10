#pragma once
#include <cassert>
#define configASSERT(value) assert(value)
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5u
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 80u
