//
// Created by sirin on 2025/12/5.
//

#ifndef SRNOS_DWT_H
#define SRNOS_DWT_H
/**
 ******************************************************************************
 * @file	Time.h
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
} SRNOS_TimeDeltaTimer;

void SRNOS_TimeInit(uint32_t cpu_freq_mhz);
uint8_t SRNOS_TimeIsReady(void);
uint32_t SRNOS_TimeGetCpuHz(void);
uint32_t SRNOS_TimeGetCounter(void);

uint32_t SRNOS_TimeGetElapsedCycles(uint32_t start_counter, uint32_t end_counter);
float SRNOS_TimeCyclesToSeconds(uint32_t cycles);
double SRNOS_TimeCyclesToSeconds64(uint32_t cycles);
uint32_t SRNOS_TimeCyclesToUs(uint32_t cycles);

void SRNOS_TimeDeltaTimerInit(SRNOS_TimeDeltaTimer *timer);
void SRNOS_TimeDeltaTimerSeed(SRNOS_TimeDeltaTimer *timer);
uint8_t SRNOS_TimeDeltaTimerIsSeeded(const SRNOS_TimeDeltaTimer *timer);
float SRNOS_TimeDeltaTimerElapsedS(SRNOS_TimeDeltaTimer *timer);
double SRNOS_TimeDeltaTimerElapsedS64(SRNOS_TimeDeltaTimer *timer);
uint32_t SRNOS_TimeDeltaTimerElapsedUs(SRNOS_TimeDeltaTimer *timer);

uint64_t SRNOS_TimeGetUptimeCycles64(void);
uint64_t SRNOS_TimeGetUptimeUs(void);
float SRNOS_TimeGetUptimeS(void);

void SRNOS_TimeDelayUs(uint32_t delay_us);
void SRNOS_TimeDelayMs(uint32_t delay_ms);

#ifdef __cplusplus
}
#endif

#endif //SRNOS_DWT_H
