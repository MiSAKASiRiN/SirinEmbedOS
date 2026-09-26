#include "SirinEmbedOS/Devices/WS2812/bsp_ws2812.h"
#include "SirinEmbedOS/Devices/Buzzer/bsp_buzzer.h"
#include "cmsis_os2.h"

extern void SRNOS_BuzzerDMAInterrupt(void);
/* DMA1 streams 0/1 are dedicated to the board buzzer/WS2812. The generated
 * handle IRQ bodies are renamed by Board CMake; these are the sole owners. */
void DMA1_Stream0_IRQHandler(void) { SRNOS_BuzzerDMAInterrupt(); }
void DMA1_Stream1_IRQHandler(void) { WS2812_DMA_IRQHandler(); }
bool SRNOS_BoardIndicatorsPrepare(void)
{
    RCC_ClkInitTypeDef clocks;
    uint32_t latency;
    HAL_RCC_GetClockConfig(&clocks, &latency);
    uint32_t timer_clock = HAL_RCC_GetPCLK2Freq();
    if (clocks.APB2CLKDivider != RCC_APB2_DIV1) timer_clock *= 2;
    WS2812_Init(timer_clock);
    WS2812_SetAll(0, 0, 0);
    return Buzzer_Init(NULL) == BUZZER_STATUS_OK;
}
void SRNOS_BoardIndicatorsWorker(void *unused)
{
    (void)unused;
    for (;;) { (void)Buzzer_Service(); osDelay(1); }
}
