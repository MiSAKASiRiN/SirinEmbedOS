//
// Created by sirin on 2025/12/5.
//
/**
 ******************************************************************************
 * @file    Time.c
 * @author  Wang Hongxi
 * @version V1.1.0
 * @date    2022/3/8
 * @brief
 ******************************************************************************
 * @attention
 *
 ******************************************************************************
 */
#include "SirinEmbedOS/Diagnostics/Time/Time.h"
#include "SirinEmbedOS/Diagnostics/Watch/Watch.h"
#include "stm32h7xx_hal.h"
#include <stddef.h>

static uint32_t s_cpu_freq_hz;
static uint8_t s_dwt_ready;
static uint8_t s_dwt_dbg_initialized;
static uint8_t s_timeline_initialized;
static uint32_t s_timeline_last_counter;
static uint64_t s_timeline_cycles64;
/* Exact integer-MHz quotient/remainder cache. Updated only by UptimeUs under
 * the same short interrupt mask as the DWT extension; no per-tick work. */
static uint64_t s_us_cycles, s_us_value;
static uint32_t s_us_remainder, s_us_frequency;

SRNOS_DEBUG_WATCH_BSS volatile SRNOS_DbgTime SRNOS_DebugDbgTime;

static uint8_t SRNOS_TimeEnsureReady(void);
static uint32_t SRNOS_TimeGetDefaultCpuFreqMHz(void);
static void SRNOS_TimeSetCpuFreqMHz(uint32_t cpu_freq_mhz);
static void SRNOS_TimeTimelineSeed(uint32_t counter);
static void SRNOS_TimeTimelineUpdate(uint32_t counter);
static uint8_t SRNOS_TimeDeltaTimerElapsedCycles(SRNOS_TimeDeltaTimer *timer, uint32_t *cycles);
static uint32_t SRNOS_TimeUsToCycles(uint32_t delay_us);
static void SRNOS_TimeDebugInitOnce(void);
static void SRNOS_TimeDebugSync(uint32_t status);
static void SRNOS_TimeDebugDelta(uint32_t counter, uint32_t cycles);
static uint32_t SRNOS_TimeIrqSave(void);
static void SRNOS_TimeIrqRestore(uint32_t primask);

static void SRNOS_TimeDebugInitOnce(void)
{
    uint32_t primask = SRNOS_TimeIrqSave();
    if (s_dwt_dbg_initialized != 0u)
    {
        SRNOS_TimeIrqRestore(primask);
        return;
    }

    SRNOS_DbgZero(&SRNOS_DebugDbgTime, sizeof(SRNOS_DebugDbgTime));
    SRNOS_DbgInit(&SRNOS_DebugDbgTime.h,
                 SRNOS_DBG_MODULE_DWT,
                 0u,
                 (uint16_t)sizeof(SRNOS_DebugDbgTime));
    s_dwt_dbg_initialized = 1u;
    SRNOS_TimeIrqRestore(primask);
}

static void SRNOS_TimeDebugSync(uint32_t status)
{
    SRNOS_TimeDebugInitOnce();
    SRNOS_DebugDbgTime.ready = SRNOS_TimeIsReady();
    SRNOS_DebugDbgTime.cpu_hz = s_cpu_freq_hz;
    SRNOS_DebugDbgTime.last_counter = DWT->CYCCNT;
    SRNOS_DbgEnd(&SRNOS_DebugDbgTime.h, HAL_GetTick(), status);
}

static void SRNOS_TimeDebugDelta(uint32_t counter, uint32_t cycles)
{
    SRNOS_TimeDebugInitOnce();
    SRNOS_DebugDbgTime.ready = SRNOS_TimeIsReady();
    SRNOS_DebugDbgTime.cpu_hz = s_cpu_freq_hz;
    SRNOS_DebugDbgTime.last_counter = counter;
    SRNOS_DebugDbgTime.last_delta_us = SRNOS_TimeCyclesToUs(cycles);
    SRNOS_DbgEnd(&SRNOS_DebugDbgTime.h, HAL_GetTick(), 0u);
}

void SRNOS_TimeInit(uint32_t cpu_freq_mhz)
{
    SRNOS_TimeDebugInitOnce();

    if (cpu_freq_mhz == 0u)
    {
        cpu_freq_mhz = SRNOS_TimeGetDefaultCpuFreqMHz();
    }

    uint32_t status = 0u;
    uint32_t primask = SRNOS_TimeIrqSave();
    SRNOS_DebugDbgTime.init_count++;

    if (cpu_freq_mhz == 0u)
    {
        s_dwt_ready = 0u;
        SRNOS_DebugDbgTime.fallback_count++;
        status = 1u;
    }
#if defined(DWT_CTRL_NOCYCCNT_Msk)
    else if ((DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) != 0u)
    {
        s_dwt_ready = 0u;
        SRNOS_DebugDbgTime.fallback_count++;
        status = 2u;
    }
#endif
    else
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        SRNOS_TimeSetCpuFreqMHz(cpu_freq_mhz);
        s_dwt_ready = 1u;

        if (s_timeline_initialized == 0u)
        {
            SRNOS_TimeTimelineSeed(DWT->CYCCNT);
        }
    }

    SRNOS_TimeIrqRestore(primask);
    SRNOS_TimeDebugSync(status);
}

uint8_t SRNOS_TimeIsReady(void)
{
    return (uint8_t)((s_dwt_ready != 0u) &&
                     (s_cpu_freq_hz != 0u) &&
                     ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) != 0u) &&
                     ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0u));
}

uint32_t SRNOS_TimeGetCpuHz(void)
{
    return s_cpu_freq_hz;
}

uint32_t SRNOS_TimeGetCounter(void)
{
    if (!SRNOS_TimeEnsureReady())
    {
        return 0u;
    }

    return DWT->CYCCNT;
}

uint32_t SRNOS_TimeGetElapsedCycles(uint32_t start_counter, uint32_t end_counter)
{
    return (uint32_t)(end_counter - start_counter);
}

float SRNOS_TimeCyclesToSeconds(uint32_t cycles)
{
    if (!SRNOS_TimeEnsureReady())
    {
        return 0.0f;
    }

    return ((float)cycles) / ((float)s_cpu_freq_hz);
}

double SRNOS_TimeCyclesToSeconds64(uint32_t cycles)
{
    if (!SRNOS_TimeEnsureReady())
    {
        return 0.0;
    }

    return ((double)cycles) / ((double)s_cpu_freq_hz);
}

uint32_t SRNOS_TimeCyclesToUs(uint32_t cycles)
{
    if (!SRNOS_TimeEnsureReady())
    {
        return 0u;
    }

    return (uint32_t)((((uint64_t)cycles) * 1000000ULL + ((uint64_t)s_cpu_freq_hz / 2ULL)) /
                      (uint64_t)s_cpu_freq_hz);
}

void SRNOS_TimeDeltaTimerInit(SRNOS_TimeDeltaTimer *timer)
{
    if (timer == NULL)
    {
        return;
    }

    timer->last_counter = 0u;
    timer->seeded = 0u;
    timer->reserved[0] = 0u;
    timer->reserved[1] = 0u;
    timer->reserved[2] = 0u;
    SRNOS_TimeDeltaTimerSeed(timer);
}

void SRNOS_TimeDeltaTimerSeed(SRNOS_TimeDeltaTimer *timer)
{
    if (timer == NULL)
    {
        return;
    }

    if (!SRNOS_TimeEnsureReady())
    {
        timer->last_counter = 0u;
        timer->seeded = 0u;
        return;
    }

    timer->last_counter = DWT->CYCCNT;
    timer->seeded = 1u;
}

uint8_t SRNOS_TimeDeltaTimerIsSeeded(const SRNOS_TimeDeltaTimer *timer)
{
    return (uint8_t)((timer != NULL) && (timer->seeded != 0u));
}

float SRNOS_TimeDeltaTimerElapsedS(SRNOS_TimeDeltaTimer *timer)
{
    uint32_t cycles = 0u;
    if (!SRNOS_TimeDeltaTimerElapsedCycles(timer, &cycles))
    {
        return 0.0f;
    }

    return SRNOS_TimeCyclesToSeconds(cycles);
}

double SRNOS_TimeDeltaTimerElapsedS64(SRNOS_TimeDeltaTimer *timer)
{
    uint32_t cycles = 0u;
    if (!SRNOS_TimeDeltaTimerElapsedCycles(timer, &cycles))
    {
        return 0.0;
    }

    return SRNOS_TimeCyclesToSeconds64(cycles);
}

uint32_t SRNOS_TimeDeltaTimerElapsedUs(SRNOS_TimeDeltaTimer *timer)
{
    uint32_t cycles = 0u;
    if (!SRNOS_TimeDeltaTimerElapsedCycles(timer, &cycles))
    {
        return 0u;
    }

    return SRNOS_TimeCyclesToUs(cycles);
}

uint64_t SRNOS_TimeGetUptimeCycles64(void)
{
    if (!SRNOS_TimeEnsureReady())
    {
        return 0u;
    }

    uint32_t primask = SRNOS_TimeIrqSave();
    SRNOS_TimeTimelineUpdate(DWT->CYCCNT);
    uint64_t cycles = s_timeline_cycles64;
    SRNOS_TimeIrqRestore(primask);
    return cycles;
}

uint64_t SRNOS_TimeGetUptimeUs(void)
{
    if (!SRNOS_TimeEnsureReady())
    {
        return 0u;
    }

    uint32_t key=SRNOS_TimeIrqSave();
    SRNOS_TimeTimelineUpdate(DWT->CYCCNT);
    uint64_t cycles=s_timeline_cycles64;
    uint32_t mhz=s_cpu_freq_hz/1000000u;
    uint64_t value;
    if(mhz && s_cpu_freq_hz%1000000u==0u) {
        uint64_t delta=cycles-s_us_cycles;
        if(s_us_frequency==s_cpu_freq_hz && delta<=UINT32_MAX) {
            uint32_t whole=(uint32_t)delta/mhz;
            uint32_t remainder=(uint32_t)delta-whole*mhz+s_us_remainder;
            s_us_value+=whole;
            if(remainder>=mhz) {++s_us_value;remainder-=mhz;}
            s_us_remainder=remainder;
        } else {
            s_us_value=cycles/mhz;
            s_us_remainder=(uint32_t)(cycles-s_us_value*mhz);
            s_us_frequency=s_cpu_freq_hz;
        }
        s_us_cycles=cycles;value=s_us_value;
    } else {
        /* Preserve the original conversion even for an overflowed/noninteger
         * legacy frequency. The fixed board always uses integer MHz. */
        uint64_t seconds=cycles/s_cpu_freq_hz;
        value=seconds*1000000ULL+(cycles-seconds*s_cpu_freq_hz)*1000000ULL/s_cpu_freq_hz;
    }
    SRNOS_TimeIrqRestore(key);
    return value;
}

float SRNOS_TimeGetUptimeS(void)
{
    return ((float)SRNOS_TimeGetUptimeUs()) * 0.000001f;
}

void SRNOS_TimeDelayUs(uint32_t delay_us)
{
    if (delay_us == 0u)
    {
        return;
    }

    if (!SRNOS_TimeEnsureReady())
    {
        uint32_t delay_ms = (delay_us + 999u) / 1000u;
        if (delay_ms == 0u)
        {
            delay_ms = 1u;
        }
        HAL_Delay(delay_ms);
        return;
    }

    uint32_t start = DWT->CYCCNT;
    uint32_t delay_cycles = SRNOS_TimeUsToCycles(delay_us);
    while ((uint32_t)(DWT->CYCCNT - start) < delay_cycles)
    {
    }
}

void SRNOS_TimeDelayMs(uint32_t delay_ms)
{
    while (delay_ms > 0u)
    {
        uint32_t chunk_ms = delay_ms;
        if (chunk_ms > 1000u)
        {
            chunk_ms = 1000u;
        }

        SRNOS_TimeDelayUs(chunk_ms * 1000u);
        delay_ms -= chunk_ms;
    }
}

static uint8_t SRNOS_TimeDeltaTimerElapsedCycles(SRNOS_TimeDeltaTimer *timer, uint32_t *cycles)
{
    if ((timer == NULL) || (cycles == NULL) || !SRNOS_TimeEnsureReady())
    {
        if (cycles != NULL)
        {
            *cycles = 0u;
        }
        return 0u;
    }

    uint32_t counter = DWT->CYCCNT;
    if (timer->seeded == 0u)
    {
        timer->last_counter = counter;
        timer->seeded = 1u;
        *cycles = 0u;
        SRNOS_TimeDebugDelta(counter, 0u);
        return 1u;
    }

    *cycles = SRNOS_TimeGetElapsedCycles(timer->last_counter, counter);
    timer->last_counter = counter;
    SRNOS_TimeDebugDelta(counter, *cycles);
    return 1u;
}

static uint8_t SRNOS_TimeEnsureReady(void)
{
    if (SRNOS_TimeIsReady())
    {
        return 1u;
    }

    SRNOS_TimeInit(0u);
    uint8_t ready = SRNOS_TimeIsReady();
    if (!ready)
    {
        SRNOS_TimeDebugInitOnce();
        SRNOS_DebugDbgTime.fallback_count++;
        SRNOS_TimeDebugSync(1u);
    }
    return ready;
}

static uint32_t SRNOS_TimeGetDefaultCpuFreqMHz(void)
{
    uint32_t cpu_hz = SystemCoreClock;
    if (cpu_hz == 0u)
    {
        SystemCoreClockUpdate();
        cpu_hz = SystemCoreClock;
    }

    return cpu_hz / 1000000u;
}

static void SRNOS_TimeSetCpuFreqMHz(uint32_t cpu_freq_mhz)
{
    s_cpu_freq_hz = cpu_freq_mhz * 1000000u;
}

static void SRNOS_TimeTimelineSeed(uint32_t counter)
{
    s_timeline_last_counter = counter;
    s_timeline_cycles64 = counter;
    s_timeline_initialized = 1u;
}

static void SRNOS_TimeTimelineUpdate(uint32_t counter)
{
    if (s_timeline_initialized == 0u)
    {
        SRNOS_TimeTimelineSeed(counter);
        return;
    }

    s_timeline_cycles64 += (uint32_t)(counter - s_timeline_last_counter);
    s_timeline_last_counter = counter;
}

static uint32_t SRNOS_TimeIrqSave(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return primask;
}

static void SRNOS_TimeIrqRestore(uint32_t primask)
{
    __DMB();
    __set_PRIMASK(primask);
}

static uint32_t SRNOS_TimeUsToCycles(uint32_t delay_us)
{
    if (!SRNOS_TimeEnsureReady())
    {
        return 0u;
    }

    uint64_t cycles = ((uint64_t)delay_us * (uint64_t)s_cpu_freq_hz) / 1000000ULL;
    if (cycles > (uint64_t)UINT32_MAX)
    {
        cycles = (uint64_t)UINT32_MAX;
    }

    return (uint32_t)cycles;
}
