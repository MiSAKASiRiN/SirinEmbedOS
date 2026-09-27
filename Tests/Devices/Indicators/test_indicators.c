#include <assert.h>
#include <stdio.h>
#include "../../../Board/Glue/indicators.c"
static uint64_t now;static unsigned commits,notes,stops;static bool busy,init_error;
static uint8_t red,green,blue;
void HAL_RCC_GetClockConfig(RCC_ClkInitTypeDef *c,uint32_t *latency) {c->APB2CLKDivider=2;*latency=0;}
uint32_t HAL_RCC_GetPCLK2Freq(void) {return 100000000;}
uint32_t HAL_GetTick(void) {return (uint32_t)(now/1000);}
uint64_t DWT_GetUptimeUs(void) {now+=1000;return now;}
void WS2812_Init(uint32_t clk) {assert(clk==200000000);}
void WS2812_SetAll(uint8_t r,uint8_t g,uint8_t b) {assert(!(r|g|b));}
void WS2812_SetPixel(uint16_t i,uint8_t r,uint8_t g,uint8_t b) {assert(i==0);red=r;green=g;blue=b;}
HAL_StatusTypeDef WS2812_Commit_StartAsync(const void *hooks) {(void)hooks;++commits;return HAL_OK;}
uint8_t WS2812_IsBusy(void) {return busy;}
void WS2812_DMA_IRQHandler(void) {}
void SRNOS_BuzzerDMAInterrupt(void) {}
void osDelay(uint32_t t) {(void)t;}
BuzzerStatus Buzzer_Init(const void *config) {(void)config;return init_error?BUZZER_STATUS_HW_ERROR:BUZZER_STATUS_OK;}
BuzzerStatus Buzzer_Start(uint32_t f) {assert(f==1000+500*notes);++notes;return BUZZER_STATUS_OK;}
BuzzerStatus Buzzer_Stop(void) {++stops;return BUZZER_STATUS_OK;}
BuzzerStatus Buzzer_Service(void) {return BUZZER_STATUS_OK;}
int main(void)
{
    assert(SRNOS_BoardIndicatorsPrepare(true));
    assert(commits==1 && notes==3 && stops==1 && now>=240000 && now<250000);
    assert(red==255 && green==255 && blue==0 && srnos_indicators.errors==0);
    assert(!SRNOS_BoardIndicatorsPrepare(true));
    SRNOS_BoardIndicatorsStep(100);SRNOS_BoardIndicatorsStep(599);assert(commits==1);
    SRNOS_BoardIndicatorsStep(600);assert(commits==2 && !red && !green);
    SRNOS_BoardIndicatorsStep(1100);assert(commits==3 && red==255 && green==255);
    busy=true;SRNOS_BoardIndicatorsStep(1600);assert(commits==3);
    busy=false;SRNOS_BoardIndicatorsStep(1601);assert(commits==4 && srnos_indicators.dma_busy==1);
    SRNOS_BoardIndicatorsStep(20000);assert(commits==5); /* no catch-up burst */
    srnos_indicators=(indicator_status_t){0};init_error=true;
    assert(SRNOS_BoardIndicatorsPrepare(true) && srnos_indicators.errors==1);
    assert(notes==3);puts("BringUp indicators: pre-scheduler one-shot yellow, bounded three-tone startup, 500ms edges, busy and failure paths passed");
}
