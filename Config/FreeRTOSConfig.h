#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>
#include "SirinEmbedOSConfig.h"
#ifndef __PICOLIBC__
#error "This kernel baseline requires the ST Arm Clang Picolibc runtime"
#endif
#ifdef __cplusplus
extern "C" {
#endif
extern uint32_t SystemCoreClock;
/* Kernel failures enter the SDK Fault recorder. */
__attribute__((noreturn)) void SRNOS_KernelFailure(const char *reason, const char *file, unsigned line);
#ifdef __cplusplus
}
#endif

#define CMSIS_device_header "stm32h7xx.h"
#define configENABLE_FPU 1
#define configENABLE_MPU 0
#define configUSE_PREEMPTION 1
#define configSUPPORT_STATIC_ALLOCATION 1
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 1
#define configCPU_CLOCK_HZ (SystemCoreClock)
#define configTICK_RATE_HZ ((TickType_t)1000)
#define configMAX_PRIORITIES 56
#define configMINIMAL_STACK_SIZE ((uint16_t)128)
#define configTOTAL_HEAP_SIZE (SRNOS_HEAP_DTCM_BYTES + SRNOS_HEAP_D1_BYTES)
#define configMAX_TASK_NAME_LEN 16
#define configUSE_16_BIT_TICKS 0
#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 1
#define configUSE_COUNTING_SEMAPHORES 1
#define configQUEUE_REGISTRY_SIZE 8
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configUSE_MALLOC_FAILED_HOOK 1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configRECORD_STACK_HIGH_ADDRESS 1
#define configUSE_CO_ROUTINES 0
#define configMAX_CO_ROUTINE_PRIORITIES 2
#define configUSE_TIMERS 1
#define configTIMER_TASK_PRIORITY 2
#define configTIMER_QUEUE_LENGTH 10
#define configTIMER_TASK_STACK_DEPTH 256
#define configGENERATE_RUN_TIME_STATS 1
#define configRUN_TIME_COUNTER_TYPE uint64_t
#define configUSE_TRACE_FACILITY 1
#define configUSE_STATS_FORMATTING_FUNCTIONS 0

/* Current ST Arm Clang runtime is Picolibc, not Newlib. */
#define configUSE_NEWLIB_REENTRANT 0
#define configUSE_PICOLIBC_TLS 1

#define configUSE_OS2_THREAD_SUSPEND_RESUME 1
#define configUSE_OS2_THREAD_ENUMERATE 1
#define configUSE_OS2_EVENTFLAGS_FROM_ISR 1
#define configUSE_OS2_THREAD_FLAGS 1
#define configUSE_OS2_TIMER 1
#define configUSE_OS2_MUTEX 1
#define INCLUDE_vTaskPrioritySet 1
#define INCLUDE_uxTaskPriorityGet 1
#define INCLUDE_vTaskDelete 1
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_xTaskDelayUntil 1
#define INCLUDE_vTaskDelay 1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_xTimerPendFunctionCall 1
#define INCLUDE_xQueueGetMutexHolder 1
#define INCLUDE_pcTaskGetTaskName 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_xTaskGetCurrentTaskHandle 1
#define INCLUDE_eTaskGetState 1

/* osKernelInitialize is the sole owner of vPortDefineHeapRegions. */
#define USE_FreeRTOS_HEAP_5
#define configHEAP_5_REGIONS sirinos_heap_regions
#define configPRIO_BITS 4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY (15 << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (5 << (8 - configPRIO_BITS))
#define configASSERT(x) do { if (!(x)) SRNOS_KernelFailure(#x, __FILE__, __LINE__); } while (0)
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler
/* CMSIS-RTOS2 owns SysTick_Handler; TIM7 remains HAL's timebase. */
#define USE_CUSTOM_SYSTICK_HANDLER_IMPLEMENTATION 0
#include "SirinEmbedOS/Diagnostics/Runtime/Trace.h"
#endif
