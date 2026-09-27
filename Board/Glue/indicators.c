#include "SirinEmbedOS/Devices/WS2812/bsp_ws2812.h"
#include "SirinEmbedOS/Devices/Buzzer/bsp_buzzer.h"
#include "SirinEmbedOS/Diagnostics/Time/bsp_dwt.h"
#include "cmsis_os2.h"

extern void SRNOS_BuzzerDMAInterrupt(void);
/* Sole DMA owners. No RTOS notifications are installed before scheduler start. */
void DMA1_Stream0_IRQHandler(void) { SRNOS_BuzzerDMAInterrupt(); }
void DMA1_Stream1_IRQHandler(void) { WS2812_DMA_IRQHandler(); }
typedef struct {
    uint32_t prepared, boot_commits, boot_notes, heartbeat_edges, dma_busy, errors;
    bool bringup, yellow;
} indicator_status_t;
volatile indicator_status_t srnos_indicators;
static uint32_t heartbeat_deadline;
static bool heartbeat_started;

bool SRNOS_BoardIndicatorsPrepare(bool bringup)
{
    if(srnos_indicators.prepared) return false;
    RCC_ClkInitTypeDef clocks;
    uint32_t latency;
    HAL_RCC_GetClockConfig(&clocks,&latency);
    uint32_t timer_clock=HAL_RCC_GetPCLK2Freq();
    if(clocks.APB2CLKDivider!=RCC_APB2_DIV1) timer_clock*=2;
    WS2812_Init(timer_clock);
    WS2812_SetAll(0,0,0);
    bool buzzer_ok=Buzzer_Init(NULL)==BUZZER_STATUS_OK;
    srnos_indicators.prepared=1;
    srnos_indicators.bringup=bringup;
    if(!buzzer_ok) ++srnos_indicators.errors;
    if(!bringup) return true;
    WS2812_SetPixel(0,255,255,0);
    if(WS2812_Commit_StartAsync(NULL)==HAL_OK) {
        srnos_indicators.boot_commits=1; srnos_indicators.yellow=true;
    } else ++srnos_indicators.errors;
    if(buzzer_ok) {
        const uint32_t tones[3]={1000,1500,2000};
        for(unsigned i=0;i<3;i++) {
            if(Buzzer_Start(tones[i])!=BUZZER_STATUS_OK) { ++srnos_indicators.errors; break; }
            ++srnos_indicators.boot_notes;
            /* DWT time is independent of scheduler/HAL tick. Spin bound also
             * protects startup if the diagnostic clock itself fails. */
            uint64_t end=DWT_GetUptimeUs()+80000;
            uint32_t spins=20000000;
            while(DWT_GetUptimeUs()<end && --spins) __NOP();
            if(!spins) { ++srnos_indicators.errors; break; }
        }
        if(Buzzer_Stop()!=BUZZER_STATUS_OK) ++srnos_indicators.errors;
    }
    return true; /* A failed indicator must not prevent board diagnosis. */
}
/* One call per worker iteration; no delay/catch-up burst on a late wake. */
void SRNOS_BoardIndicatorsStep(uint32_t now)
{
    (void)Buzzer_Service();
    if(!srnos_indicators.bringup) return;
    if(!heartbeat_started) { heartbeat_started=true; heartbeat_deadline=now+500; }
    if((int32_t)(now-heartbeat_deadline)<0) return;
    if(WS2812_IsBusy()) { ++srnos_indicators.dma_busy; return; }
    bool yellow=!srnos_indicators.yellow;
    WS2812_SetPixel(0,yellow?255:0,yellow?255:0,0);
    HAL_StatusTypeDef result=WS2812_Commit_StartAsync(NULL);
    if(result==HAL_OK) { srnos_indicators.yellow=yellow; ++srnos_indicators.heartbeat_edges; }
    else ++srnos_indicators.errors;
    heartbeat_deadline=now+500;
}
void SRNOS_BoardIndicatorsWorker(void *unused)
{
    (void)unused;
    for(;;) { SRNOS_BoardIndicatorsStep(HAL_GetTick()); osDelay(1); }
}
