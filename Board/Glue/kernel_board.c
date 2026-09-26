#include "sirinos_board.h"
#include "kernel_internal.h"
#include "FreeRTOS.h"
#include "stm32h7xx.h"

/* NOLOAD heaps contain no initial values. heap_5 initializes its own metadata
 * before any allocation; allocated payloads are deliberately not zero-filled.
 * DTCM is not DMA-accessible; D1 is cacheable. Neither heap is a DMA allocator.
 */
__attribute__((section(".sirinos_heap_dtcm"), aligned(32)))
static uint8_t dtcm_heap[SRNOS_HEAP_DTCM_BYTES];
__attribute__((section(".sirinos_heap_d1"), aligned(32)))
static uint8_t d1_heap[SRNOS_HEAP_D1_BYTES];
HeapRegion_t sirinos_heap_regions[] = {
    {dtcm_heap, sizeof(dtcm_heap)},
    {d1_heap, sizeof(d1_heap)},
    {NULL, 0}
};

void SRNOS_BoardStartKernel(void)
{
    configASSERT(__get_IPSR() == 0U);
    configASSERT(__get_PRIMASK() == 0U);
    configASSERT(NVIC_GetPriorityGrouping() == 3U); /* Four preemption bits. */
    SRNOS_KernelStart();
}
