#include "FreeRTOS.h"
#include "task.h"
#include "stm32h7xx.h"

#include "SirinEmbedOS/Core/C/Fault.h"
#include "Diagnostics/Runtime/trace_internal.h"

void SRNOS_KernelFailure(const char *reason, const char *file, unsigned line)
{
    __disable_irq();
    SRNOS_Fault_Panic(SRNOS_FAULT_REASON_ASSERTION, reason, file, __func__, line);
}

void vApplicationMallocFailedHook(void)
{
    SRNOS_KernelFailure("FreeRTOS heap exhausted", __FILE__, __LINE__);
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    __disable_irq();
    SRNOS_Fault_Panic(SRNOS_FAULT_REASON_KERNEL_STACK_OVERFLOW,
                      name, __FILE__, __func__, __LINE__);
}

void vApplicationTickHook(void) { SRNOS_DiagnosticsTick(); }

void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack, uint32_t *depth)
{
    static StaticTask_t idle_tcb;
    static StackType_t idle_stack[configMINIMAL_STACK_SIZE];
    *tcb = &idle_tcb;
    *stack = idle_stack;
    *depth = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **tcb, StackType_t **stack, uint32_t *depth)
{
    static StaticTask_t timer_tcb;
    static StackType_t timer_stack[configTIMER_TASK_STACK_DEPTH];
    *tcb = &timer_tcb;
    *stack = timer_stack;
    *depth = configTIMER_TASK_STACK_DEPTH;
}
