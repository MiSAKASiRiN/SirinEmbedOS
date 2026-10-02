#include "kernel_test_adapter.h"
#include "FreeRTOS.h"
void *SRNOS_EEPROMTestAllocate(size_t bytes) {return pvPortMalloc(bytes);}

#include "stm32h7xx_hal.h"
#include "SirinEmbedOS/Kernel/Thread.h"
bool SRNOS_EEPROMTestObserveSCL(uint8_t *workspace, volatile SRNOS_EEPROMTestSCL *snapshot)
{
    snapshot->core_hz=SystemCoreClock;
    SRNOS_EEPROMStatus status=SRNOS_EEPROMReadAsync(0,workspace,32,100);
    if(status==SRNOS_EEPROM_OK) status=SRNOS_EEPROMPoll();
    uint32_t start=DWT->CYCCNT, previous=0;
    bool was_high=(GPIOB->IDR & GPIO_PIN_8)!=0;
    while(status==SRNOS_EEPROM_IN_PROGRESS && snapshot->count<128 &&
          DWT->CYCCNT-start<SystemCoreClock/500U) {
        bool high=(GPIOB->IDR & GPIO_PIN_8)!=0;
        uint32_t now=DWT->CYCCNT;
        if(high && !was_high) {
            if(previous) snapshot->cycles[snapshot->count++]=now-previous;
            previous=now;
        }
        was_high=high;
    }
    while(status==SRNOS_EEPROM_IN_PROGRESS) { SRNOS_Delay(1);status=SRNOS_EEPROMPoll(); }
    snapshot->status=status;
    return status==SRNOS_EEPROM_OK && snapshot->count>32;
}

void SRNOS_EEPROMTestPublishBuffer(const void *data, size_t bytes)
{
    uintptr_t begin=(uintptr_t)data & ~(uintptr_t)31;
    uintptr_t end=((uintptr_t)data+bytes+31U) & ~(uintptr_t)31;
    SCB_CleanDCache_by_Addr((uint32_t *)begin,(int32_t)(end-begin));
    __DSB();
}

/* Test-only SWD upload: publish before halting, then invalidate CPU copies. */
void SRNOS_EEPROMTestAcquireBuffer(void *data, size_t bytes)
{
    uintptr_t begin=(uintptr_t)data & ~(uintptr_t)31;
    uintptr_t end=((uintptr_t)data+bytes+31U) & ~(uintptr_t)31;
    SCB_InvalidateDCache_by_Addr((uint32_t *)begin,(int32_t)(end-begin));
    __DSB();
}
