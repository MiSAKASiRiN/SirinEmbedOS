//
// Created by sirin on 2025/12/5.
//

#ifndef H723VG_V2_FREERTOS_BSP_DWT_H
#define H723VG_V2_FREERTOS_BSP_DWT_H
/**
 ******************************************************************************
 * @file	bsp_dwt.h
 * @author  Wang Hongxi
 * @version V1.1.0
 * @date    2022/3/8
 * @brief
 ******************************************************************************
 * @attention
 *
 ******************************************************************************
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint32_t last_counter;
    uint8_t seeded;
    uint8_t reserved[3];
} DWT_DeltaTimer_t;

void DWT_Init(uint32_t cpu_freq_mhz);
uint8_t DWT_IsReady(void);
uint32_t DWT_GetCpuHz(void);
uint32_t DWT_GetCounter(void);

uint32_t DWT_GetElapsedCycles(uint32_t start_counter, uint32_t end_counter);
float DWT_CyclesToSeconds(uint32_t cycles);
double DWT_CyclesToSeconds64(uint32_t cycles);
uint32_t DWT_CyclesToUs(uint32_t cycles);

void DWT_DeltaTimerInit(DWT_DeltaTimer_t *timer);
void DWT_DeltaTimerSeed(DWT_DeltaTimer_t *timer);
uint8_t DWT_DeltaTimerIsSeeded(const DWT_DeltaTimer_t *timer);
float DWT_DeltaTimerElapsedS(DWT_DeltaTimer_t *timer);
double DWT_DeltaTimerElapsedS64(DWT_DeltaTimer_t *timer);
uint32_t DWT_DeltaTimerElapsedUs(DWT_DeltaTimer_t *timer);

uint64_t DWT_GetUptimeCycles64(void);
uint64_t DWT_GetUptimeUs(void);
float DWT_GetUptimeS(void);

void DWT_DelayUs(uint32_t delay_us);
void DWT_DelayMs(uint32_t delay_ms);

#ifdef __cplusplus
}
#endif

#endif //H723VG_V2_FREERTOS_BSP_DWT_H
