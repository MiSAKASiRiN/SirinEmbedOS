#include "thread_internal.h"
#include "indicators_internal.h"
#include "Devices/indicators_board.h"
#include "SirinEmbedOS/Devices/WS2812/WS2812.h"
#include "Devices/Buzzer/buzzer_service.h"
#include "SirinEmbedOS/Diagnostics/Time/Time.h"
#include "cmsis_os2.h"
#include "stm32h7xx_hal.h"

typedef struct {
    uint32_t prepared, boot_commits, boot_notes, heartbeat_edges, dma_busy, errors;
    bool bringup, yellow;
} indicator_status_t;
volatile indicator_status_t SRNOS_IndicatorsState;
static uint32_t heartbeat_deadline;
static bool heartbeat_started;
static osThreadId_t indicator_thread;

bool SRNOS_IndicatorsPrepare(bool bringup)
{
    if(SRNOS_IndicatorsState.prepared) return false;
    bool buzzer_ok=SRNOS_BoardIndicatorsInitialize();
    SRNOS_IndicatorsState.prepared=1;
    SRNOS_IndicatorsState.bringup=bringup;
    if(!buzzer_ok) ++SRNOS_IndicatorsState.errors;
    if(!bringup) return true;
    SRNOS_WS2812SetPixel(0,255,255,0);
    if(SRNOS_WS2812CommitStartAsync(NULL)==SRNOS_WS2812_OK) {
        SRNOS_IndicatorsState.boot_commits=1; SRNOS_IndicatorsState.yellow=true;
    } else ++SRNOS_IndicatorsState.errors;
    if(buzzer_ok) {
        const uint32_t tones[3]={1000,1500,2000};
        for(unsigned i=0;i<3;i++) {
            if(SRNOS_BuzzerStart(tones[i])!=SRNOS_BUZZER_STATUS_OK) { ++SRNOS_IndicatorsState.errors; break; }
            ++SRNOS_IndicatorsState.boot_notes;
            /* DWT time is independent of scheduler/HAL tick. Spin bound also
             * protects startup if the diagnostic clock itself fails. */
            uint64_t end=SRNOS_TimeGetUptimeUs()+80000;
            uint32_t spins=20000000;
            while(SRNOS_TimeGetUptimeUs()<end && --spins) __NOP();
            if(!spins) { ++SRNOS_IndicatorsState.errors; break; }
        }
        if(SRNOS_BuzzerStop()!=SRNOS_BUZZER_STATUS_OK) ++SRNOS_IndicatorsState.errors;
    }
    return true; /* A failed indicator must not prevent board diagnosis. */
}
/* One call per worker iteration; no delay/catch-up burst on a late wake. */
void SRNOS_IndicatorsStep(uint32_t now)
{
    (void)SRNOS_BuzzerService();
    if(!SRNOS_IndicatorsState.bringup) return;
    if(!heartbeat_started) { heartbeat_started=true; heartbeat_deadline=now+500; }
    if((int32_t)(now-heartbeat_deadline)<0) return;
    if(SRNOS_WS2812IsBusy()) { ++SRNOS_IndicatorsState.dma_busy; return; }
    bool yellow=!SRNOS_IndicatorsState.yellow;
    SRNOS_WS2812SetPixel(0,yellow?255:0,yellow?255:0,0);
    SRNOS_WS2812Result result=SRNOS_WS2812CommitStartAsync(NULL);
    if(result==SRNOS_WS2812_OK) { SRNOS_IndicatorsState.yellow=yellow; ++SRNOS_IndicatorsState.heartbeat_edges; }
    else ++SRNOS_IndicatorsState.errors;
    heartbeat_deadline=now+500;
}
static void indicator_step(void *unused) { (void)unused;SRNOS_IndicatorsStep(HAL_GetTick()); }
static uint32_t indicator_next_wait(uint32_t now)
{
    uint32_t wait=SRNOS_BuzzerServiceNextWait(now);
    if(SRNOS_IndicatorsState.bringup && heartbeat_started) {
        int32_t left=(int32_t)(heartbeat_deadline-now);
        /* Only an already-due heartbeat waiting for a short LED DMA uses a
         * 1 tick retry; idle indicators never wake at 1 kHz. */
        uint32_t heartbeat_wait=left<=0?1:(uint32_t)left;
        if(heartbeat_wait<wait)wait=heartbeat_wait;
    }
    return wait;
}
static void indicator_wake(bool from_isr)
{
    (void)from_isr;if(indicator_thread)(void)osThreadFlagsSet(indicator_thread,1u);
}
static bool indicator_wait(void *unused) {
    (void)unused;(void)osThreadFlagsWait(1u,osFlagsWaitAny,indicator_next_wait(HAL_GetTick()));return true;
}
void SRNOS_IndicatorsWorker(void *unused)
{
    (void)unused;indicator_thread=osThreadGetId();SRNOS_BuzzerSetServiceWake(indicator_wake);
    const SRNOS_InternalLoop loop={indicator_step,indicator_wait,NULL,0,false};
    SRNOS_KernelRunLoop(&loop);
}
