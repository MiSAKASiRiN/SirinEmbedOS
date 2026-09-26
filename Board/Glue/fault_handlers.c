#include "STM32H7Fault.h"

/* Naked veneers preserve the exception LR and original MSP. A C prologue
 * before mrs would corrupt the frame address. All six vectors are SDK-owned. */
#define SRNOS_FAULT_VECTOR(name, reason) \
    __attribute__((naked, noreturn)) void name(void) { \
        __asm volatile("mrs r0, msp\n" "mrs r1, psp\n" "mov r2, lr\n" \
                       "movs r3, #" #reason "\n" \
                       "b SRNOS_STM32H7_HandleException\n"); \
    }
SRNOS_FAULT_VECTOR(HardFault_Handler, 16)
SRNOS_FAULT_VECTOR(MemManage_Handler, 17)
SRNOS_FAULT_VECTOR(BusFault_Handler, 18)
SRNOS_FAULT_VECTOR(UsageFault_Handler, 19)
SRNOS_FAULT_VECTOR(NMI_Handler, 20)
SRNOS_FAULT_VECTOR(DebugMon_Handler, 21)
