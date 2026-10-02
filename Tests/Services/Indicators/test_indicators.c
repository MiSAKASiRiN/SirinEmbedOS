#include <assert.h>
#include <stdio.h>
#include "../../../Board/Support/Devices/indicators_board.c"
#include "../../../Source/Services/Indicators/indicators.c"
static uint64_t now;static unsigned commits,notes,stops;static bool busy,init_error;
static uint8_t red,green,blue;
void HAL_RCC_GetClockConfig(RCC_ClkInitTypeDef *c,uint32_t *latency) {c->APB2CLKDivider=2;*latency=0;}
uint32_t HAL_RCC_GetPCLK2Freq(void) {return 100000000;}
uint32_t HAL_GetTick(void) {return (uint32_t)(now/1000);}
uint64_t SRNOS_TimeGetUptimeUs(void) {now+=1000;return now;}
void SRNOS_WS2812Init(uint32_t clk) {assert(clk==200000000);}
void SRNOS_WS2812SetAll(uint8_t r,uint8_t g,uint8_t b) {assert(!(r|g|b));}
void SRNOS_WS2812SetPixel(uint16_t i,uint8_t r,uint8_t g,uint8_t b) {assert(i==0);red=r;green=g;blue=b;}
SRNOS_WS2812Result SRNOS_WS2812CommitStartAsync(const void *hooks) {(void)hooks;++commits;return SRNOS_WS2812_OK;}
uint8_t SRNOS_WS2812IsBusy(void) {return busy;}
void SRNOS_WS2812DMAIRQHandler(void) {}
void SRNOS_BuzzerDMAInterrupt(void) {}
void osDelay(uint32_t t) {(void)t;}
SRNOS_BuzzerStatus SRNOS_BuzzerInit(const SRNOS_BuzzerConfig *config) {(void)config;return init_error?SRNOS_BUZZER_STATUS_HW_ERROR:SRNOS_BUZZER_STATUS_OK;}
SRNOS_BuzzerStatus SRNOS_BuzzerStart(uint32_t f) {assert(f==1000+500*notes);++notes;return SRNOS_BUZZER_STATUS_OK;}
SRNOS_BuzzerStatus SRNOS_BuzzerStop(void) {++stops;return SRNOS_BUZZER_STATUS_OK;}
SRNOS_BuzzerStatus SRNOS_BuzzerService(void) {return SRNOS_BUZZER_STATUS_OK;}
static uint32_t buzzer_wait=UINT32_MAX,wait_timeout,wakes;
void SRNOS_BuzzerSetServiceWake(SRNOS_BuzzerWakeFn fn) {(void)fn;}
uint32_t SRNOS_BuzzerServiceNextWait(uint32_t time) {(void)time;return buzzer_wait;}
osThreadId_t osThreadGetId(void) {return (void *)1;}
uint32_t osThreadFlagsSet(osThreadId_t t,uint32_t f) {assert(t);++wakes;return f;}
uint32_t osThreadFlagsWait(uint32_t f,uint32_t options,uint32_t timeout) {(void)f;(void)options;wait_timeout=timeout;return 0;}
int main(void)
{
    assert(SRNOS_IndicatorsPrepare(false));
    assert(indicator_next_wait(123)==UINT32_MAX);
    buzzer_wait=17;assert(indicator_next_wait(123)==17);buzzer_wait=UINT32_MAX;
    indicator_thread=(void *)1;indicator_wake(false);indicator_wake(true);assert(wakes==2);
    indicator_wait(NULL);assert(wait_timeout==UINT32_MAX);
    assert(commits==0 && notes==0 && stops==0);
    SRNOS_IndicatorsStep(20000); assert(commits==0);
    assert(!SRNOS_IndicatorsPrepare(true));
    SRNOS_IndicatorsState=(indicator_status_t){0};
    assert(SRNOS_IndicatorsPrepare(true));
    assert(commits==1 && notes==3 && stops==1 && now>=240000 && now<250000);
    assert(red==255 && green==255 && blue==0 && SRNOS_IndicatorsState.errors==0);
    assert(!SRNOS_IndicatorsPrepare(true));
    SRNOS_IndicatorsStep(100);assert(indicator_next_wait(100)==500);assert(indicator_next_wait(599)==1);SRNOS_IndicatorsStep(599);assert(commits==1);
    SRNOS_IndicatorsStep(600);assert(commits==2 && !red && !green);
    SRNOS_IndicatorsStep(1100);assert(commits==3 && red==255 && green==255);
    busy=true;SRNOS_IndicatorsStep(1600);assert(commits==3);
    busy=false;SRNOS_IndicatorsStep(1601);assert(commits==4 && SRNOS_IndicatorsState.dma_busy==1);
    SRNOS_IndicatorsStep(20000);assert(commits==5); /* no catch-up burst */
    heartbeat_deadline=3;heartbeat_started=true;
    assert(indicator_next_wait(UINT32_MAX-2)==6);
    SRNOS_IndicatorsState=(indicator_status_t){0};init_error=true;
    assert(SRNOS_IndicatorsPrepare(true) && SRNOS_IndicatorsState.errors==1);
    assert(notes==3);puts("BringUp indicators: pre-scheduler one-shot yellow, bounded three-tone startup, 500ms edges, busy and failure paths passed");
}
