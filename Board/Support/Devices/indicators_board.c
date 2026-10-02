#include "Devices/WS2812/ws2812_internal.h"
#include "Devices/Buzzer/buzzer_service.h"
#include "Devices/indicators_board.h"

extern void SRNOS_BuzzerDMAInterrupt(void);
/* Sole DMA owners. No RTOS notifications are installed before scheduler start. */
void DMA1_Stream0_IRQHandler(void) { SRNOS_BuzzerDMAInterrupt(); }
void DMA1_Stream1_IRQHandler(void) { SRNOS_WS2812DMAIRQHandler(); }

bool SRNOS_BoardIndicatorsInitialize(void)
{
    RCC_ClkInitTypeDef clocks;
    uint32_t latency;
    HAL_RCC_GetClockConfig(&clocks,&latency);
    uint32_t timer_clock=HAL_RCC_GetPCLK2Freq();
    if(clocks.APB2CLKDivider!=RCC_APB2_DIV1) timer_clock*=2;
    SRNOS_WS2812Init(timer_clock);
    SRNOS_WS2812SetAll(0,0,0);
    bool buzzer_ok=SRNOS_BuzzerInit(NULL)==SRNOS_BUZZER_STATUS_OK;
    return buzzer_ok;
}
