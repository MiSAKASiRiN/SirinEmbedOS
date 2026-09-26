#include "stm32h7xx.h"
#include "SirinEmbedOS/Core/C/Platform.h"
#include "SirinEmbedOS/Core/Memory/mem_sections.h"

/* Called from main USER CODE 1, before the generated MPU/cache setup. */
void SRNOS_BoardEarlyInitialize(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_SRAM1EN | RCC_AHB2ENR_SRAM2EN;
    (void)RCC->AHB2ENR;
    __DSB();
    SCB->ITCMCR |= SCB_ITCMCR_EN_Msk;
    __DSB();
    __ISB();
    omx_mem_init_early();
    SRNOS_Platform_Initialize();
}
