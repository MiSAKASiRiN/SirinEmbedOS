#ifndef SRNOS_KERNEL_INTERNAL_H
#define SRNOS_KERNEL_INTERNAL_H
#include <stdbool.h>
bool SRNOS_BoardBootIdentityInitialize(void);
/* Board boot only: call once, after HAL initialization, in privileged context. */
__attribute__((noreturn)) void SRNOS_KernelStart(void);
#endif
