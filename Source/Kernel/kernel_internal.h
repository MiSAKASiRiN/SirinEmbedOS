#ifndef SRNOS_KERNEL_INTERNAL_H
#define SRNOS_KERNEL_INTERNAL_H
#include <stdbool.h>
#include "SirinEmbedOS/Kernel/System.h"
bool SRNOS_BoardBootIdentityInitialize(void);
uint64_t SRNOS_KernelStartupHeapBudget(const SRNOS_SystemConfig *config);
/* Board boot only: call once, after HAL initialization, in privileged context. */
__attribute__((noreturn)) void SRNOS_KernelStart(void);
#endif
