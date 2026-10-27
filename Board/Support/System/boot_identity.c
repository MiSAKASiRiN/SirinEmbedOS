#include "SirinEmbedOS/Kernel/System.h"
#include "kernel_internal.h"
#include "stm32h7xx_hal.h"

/* Fixed STM32H723 RNG v3.2. Startup owns it; no IRQ, DMA or runtime service.
 * Conditioning/health-test sequence follows ST's HAL RNG initialization.
 * HSI48 is already enabled by the board clock configuration. */
static uint64_t boot_id;
uint64_t SRNOS_SystemBootId(void) { return boot_id; }

static bool wait_ready(uint32_t *word)
{
    const uint32_t start=HAL_GetTick();
    for (uint32_t budget=1000000u;budget;--budget) {
        uint32_t status=RNG->SR;
        if (status&(RNG_SR_CECS|RNG_SR_SECS|RNG_SR_CEIS|RNG_SR_SEIS)) return false;
        if (status&RNG_SR_DRDY) {
            const uint32_t value=RNG->DR;
            if (RNG->SR&(RNG_SR_CECS|RNG_SR_SECS|RNG_SR_CEIS|RNG_SR_SEIS)) return false;
            *word=value;return true;
        }
        if ((uint32_t)(HAL_GetTick()-start)>5u) return false;
    }
    return false;
}
bool SRNOS_BoardBootIdentityInitialize(void)
{
    if (boot_id) return true;
    if (!(RCC->CR&RCC_CR_HSI48RDY)) return false;
    __HAL_RCC_RNG_CONFIG(RCC_RNGCLKSOURCE_HSI48);
    __HAL_RCC_RNG_CLK_ENABLE();
    for (unsigned attempt=0;attempt<3 && !boot_id;++attempt) {
        __HAL_RCC_RNG_FORCE_RESET();
        __HAL_RCC_RNG_RELEASE_RESET();
        SET_BIT(RNG->CR,RNG_CR_CONDRST);
        RNG->HTCR=0x17590ABCu;
        RNG->HTCR=0x00007274u;
        CLEAR_BIT(RNG->CR,RNG_CR_CONDRST);
        const uint32_t start=HAL_GetTick();
        uint32_t budget=1000000u;
        while ((RNG->CR&RNG_CR_CONDRST) && budget && (uint32_t)(HAL_GetTick()-start)<=5u) --budget;
        if (RNG->CR&RNG_CR_CONDRST) continue;
        SET_BIT(RNG->CR,RNG_CR_RNGEN);
        uint32_t low=0,high=0;
        if (wait_ready(&low) && wait_ready(&high) && low!=high)
            boot_id=((uint64_t)high<<32)|low;
    }
    CLEAR_BIT(RNG->CR,RNG_CR_RNGEN);
    __HAL_RCC_RNG_CLK_DISABLE();
    return boot_id!=0;
}
