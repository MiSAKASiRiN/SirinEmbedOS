//
// Created by sirin on 2025/12/5.
//
/**
 ******************************************************************************
 * @file    bsp_dwt.c
 * @author  Wang Hongxi
 * @version V1.1.0
 * @date    2022/3/8
 * @brief
 ******************************************************************************
 * @attention
 *
 ******************************************************************************
 */
#include "SirinEmbedOS/Diagnostics/Time/bsp_dwt.h"
#include "SirinEmbedOS/Diagnostics/Watch/omx_debug_watch.h"
#include "stm32h7xx_hal.h"
#include <stddef.h>

static uint32_t s_cpu_freq_hz;
static uint8_t s_dwt_ready;
static uint8_t s_dwt_dbg_initialized;
static uint8_t s_timeline_initialized;
static uint32_t s_timeline_last_counter;
static uint64_t s_timeline_cycles64;

OMX_DEBUG_WATCH_BSS volatile omx_dbg_dwt_t g_omx_dbg_dwt;

static uint8_t DWT_EnsureReady(void);
static uint32_t DWT_GetDefaultCpuFreqMHz(void);
static void DWT_SetCpuFreqMHz(uint32_t cpu_freq_mhz);
static void DWT_TimelineSeed(uint32_t counter);
static void DWT_TimelineUpdate(uint32_t counter);
static uint8_t DWT_DeltaTimerElapsedCycles(DWT_DeltaTimer_t *timer, uint32_t *cycles);
static uint32_t DWT_UsToCycles(uint32_t delay_us);
static void DWT_DebugInitOnce(void);
static void DWT_DebugSync(uint32_t status);
static void DWT_DebugDelta(uint32_t counter, uint32_t cycles);
static uint32_t DWT_IrqSave(void);
static void DWT_IrqRestore(uint32_t primask);

static void DWT_DebugInitOnce(void)
{
    uint32_t primask = DWT_IrqSave();
    if (s_dwt_dbg_initialized != 0u)
    {
        DWT_IrqRestore(primask);
        return;
    }

    omx_dbg_zero(&g_omx_dbg_dwt, sizeof(g_omx_dbg_dwt));
    omx_dbg_init(&g_omx_dbg_dwt.h,
                 OMX_DBG_MODULE_DWT,
                 0u,
                 (uint16_t)sizeof(g_omx_dbg_dwt));
    s_dwt_dbg_initialized = 1u;
    DWT_IrqRestore(primask);
}

static void DWT_DebugSync(uint32_t status)
{
    DWT_DebugInitOnce();
    g_omx_dbg_dwt.ready = DWT_IsReady();
    g_omx_dbg_dwt.cpu_hz = s_cpu_freq_hz;
    g_omx_dbg_dwt.last_counter = DWT->CYCCNT;
    omx_dbg_end(&g_omx_dbg_dwt.h, HAL_GetTick(), status);
}

static void DWT_DebugDelta(uint32_t counter, uint32_t cycles)
{
    DWT_DebugInitOnce();
    g_omx_dbg_dwt.ready = DWT_IsReady();
    g_omx_dbg_dwt.cpu_hz = s_cpu_freq_hz;
    g_omx_dbg_dwt.last_counter = counter;
    g_omx_dbg_dwt.last_delta_us = DWT_CyclesToUs(cycles);
    omx_dbg_end(&g_omx_dbg_dwt.h, HAL_GetTick(), 0u);
}

void DWT_Init(uint32_t cpu_freq_mhz)
{
    DWT_DebugInitOnce();

    if (cpu_freq_mhz == 0u)
    {
        cpu_freq_mhz = DWT_GetDefaultCpuFreqMHz();
    }

    uint32_t status = 0u;
    uint32_t primask = DWT_IrqSave();
    g_omx_dbg_dwt.init_count++;

    if (cpu_freq_mhz == 0u)
    {
        s_dwt_ready = 0u;
        g_omx_dbg_dwt.fallback_count++;
        status = 1u;
    }
#if defined(DWT_CTRL_NOCYCCNT_Msk)
    else if ((DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) != 0u)
    {
        s_dwt_ready = 0u;
        g_omx_dbg_dwt.fallback_count++;
        status = 2u;
    }
#endif
    else
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        DWT_SetCpuFreqMHz(cpu_freq_mhz);
        s_dwt_ready = 1u;

        if (s_timeline_initialized == 0u)
        {
            DWT_TimelineSeed(DWT->CYCCNT);
        }
    }

    DWT_IrqRestore(primask);
    DWT_DebugSync(status);
}

uint8_t DWT_IsReady(void)
{
    return (uint8_t)((s_dwt_ready != 0u) &&
                     (s_cpu_freq_hz != 0u) &&
                     ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) != 0u) &&
                     ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0u));
}

uint32_t DWT_GetCpuHz(void)
{
    return s_cpu_freq_hz;
}

uint32_t DWT_GetCounter(void)
{
    if (!DWT_EnsureReady())
    {
        return 0u;
    }

    return DWT->CYCCNT;
}

uint32_t DWT_GetElapsedCycles(uint32_t start_counter, uint32_t end_counter)
{
    return (uint32_t)(end_counter - start_counter);
}

float DWT_CyclesToSeconds(uint32_t cycles)
{
    if (!DWT_EnsureReady())
    {
        return 0.0f;
    }

    return ((float)cycles) / ((float)s_cpu_freq_hz);
}

double DWT_CyclesToSeconds64(uint32_t cycles)
{
    if (!DWT_EnsureReady())
    {
        return 0.0;
    }

    return ((double)cycles) / ((double)s_cpu_freq_hz);
}

uint32_t DWT_CyclesToUs(uint32_t cycles)
{
    if (!DWT_EnsureReady())
    {
        return 0u;
    }

    return (uint32_t)((((uint64_t)cycles) * 1000000ULL + ((uint64_t)s_cpu_freq_hz / 2ULL)) /
                      (uint64_t)s_cpu_freq_hz);
}

void DWT_DeltaTimerInit(DWT_DeltaTimer_t *timer)
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
    DWT_DeltaTimerSeed(timer);
}

void DWT_DeltaTimerSeed(DWT_DeltaTimer_t *timer)
{
    if (timer == NULL)
    {
        return;
    }

    if (!DWT_EnsureReady())
    {
        timer->last_counter = 0u;
        timer->seeded = 0u;
        return;
    }

    timer->last_counter = DWT->CYCCNT;
    timer->seeded = 1u;
}

uint8_t DWT_DeltaTimerIsSeeded(const DWT_DeltaTimer_t *timer)
{
    return (uint8_t)((timer != NULL) && (timer->seeded != 0u));
}

float DWT_DeltaTimerElapsedS(DWT_DeltaTimer_t *timer)
{
    uint32_t cycles = 0u;
    if (!DWT_DeltaTimerElapsedCycles(timer, &cycles))
    {
        return 0.0f;
    }

    return DWT_CyclesToSeconds(cycles);
}

double DWT_DeltaTimerElapsedS64(DWT_DeltaTimer_t *timer)
{
    uint32_t cycles = 0u;
    if (!DWT_DeltaTimerElapsedCycles(timer, &cycles))
    {
        return 0.0;
    }

    return DWT_CyclesToSeconds64(cycles);
}

uint32_t DWT_DeltaTimerElapsedUs(DWT_DeltaTimer_t *timer)
{
    uint32_t cycles = 0u;
    if (!DWT_DeltaTimerElapsedCycles(timer, &cycles))
    {
        return 0u;
    }

    return DWT_CyclesToUs(cycles);
}

uint64_t DWT_GetUptimeCycles64(void)
{
    if (!DWT_EnsureReady())
    {
        return 0u;
    }

    uint32_t primask = DWT_IrqSave();
    DWT_TimelineUpdate(DWT->CYCCNT);
    uint64_t cycles = s_timeline_cycles64;
    DWT_IrqRestore(primask);
    return cycles;
}

uint64_t DWT_GetUptimeUs(void)
{
    if (!DWT_EnsureReady())
    {
        return 0u;
    }

    uint64_t cycles = DWT_GetUptimeCycles64();
    uint64_t seconds = cycles / (uint64_t)s_cpu_freq_hz;
    uint64_t remainder = cycles - seconds * (uint64_t)s_cpu_freq_hz;

    return seconds * 1000000ULL + (remainder * 1000000ULL) / (uint64_t)s_cpu_freq_hz;
}

float DWT_GetUptimeS(void)
{
    return ((float)DWT_GetUptimeUs()) * 0.000001f;
}

void DWT_DelayUs(uint32_t delay_us)
{
    if (delay_us == 0u)
    {
        return;
    }

    if (!DWT_EnsureReady())
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
    uint32_t delay_cycles = DWT_UsToCycles(delay_us);
    while ((uint32_t)(DWT->CYCCNT - start) < delay_cycles)
    {
    }
}

void DWT_DelayMs(uint32_t delay_ms)
{
    while (delay_ms > 0u)
    {
        uint32_t chunk_ms = delay_ms;
        if (chunk_ms > 1000u)
        {
            chunk_ms = 1000u;
        }

        DWT_DelayUs(chunk_ms * 1000u);
        delay_ms -= chunk_ms;
    }
}

static uint8_t DWT_DeltaTimerElapsedCycles(DWT_DeltaTimer_t *timer, uint32_t *cycles)
{
    if ((timer == NULL) || (cycles == NULL) || !DWT_EnsureReady())
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
        DWT_DebugDelta(counter, 0u);
        return 1u;
    }

    *cycles = DWT_GetElapsedCycles(timer->last_counter, counter);
    timer->last_counter = counter;
    DWT_DebugDelta(counter, *cycles);
    return 1u;
}

static uint8_t DWT_EnsureReady(void)
{
    if (DWT_IsReady())
    {
        return 1u;
    }

    DWT_Init(0u);
    uint8_t ready = DWT_IsReady();
    if (!ready)
    {
        DWT_DebugInitOnce();
        g_omx_dbg_dwt.fallback_count++;
        DWT_DebugSync(1u);
    }
    return ready;
}

static uint32_t DWT_GetDefaultCpuFreqMHz(void)
{
    uint32_t cpu_hz = SystemCoreClock;
    if (cpu_hz == 0u)
    {
        SystemCoreClockUpdate();
        cpu_hz = SystemCoreClock;
    }

    return cpu_hz / 1000000u;
}

static void DWT_SetCpuFreqMHz(uint32_t cpu_freq_mhz)
{
    s_cpu_freq_hz = cpu_freq_mhz * 1000000u;
}

static void DWT_TimelineSeed(uint32_t counter)
{
    s_timeline_last_counter = counter;
    s_timeline_cycles64 = counter;
    s_timeline_initialized = 1u;
}

static void DWT_TimelineUpdate(uint32_t counter)
{
    if (s_timeline_initialized == 0u)
    {
        DWT_TimelineSeed(counter);
        return;
    }

    s_timeline_cycles64 += (uint32_t)(counter - s_timeline_last_counter);
    s_timeline_last_counter = counter;
}

static uint32_t DWT_IrqSave(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return primask;
}

static void DWT_IrqRestore(uint32_t primask)
{
    __DMB();
    __set_PRIMASK(primask);
}

static uint32_t DWT_UsToCycles(uint32_t delay_us)
{
    if (!DWT_EnsureReady())
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
