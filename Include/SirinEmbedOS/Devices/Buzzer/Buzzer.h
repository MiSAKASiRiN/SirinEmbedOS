#ifndef SRNOS_BUZZER_H
#define SRNOS_BUZZER_H

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifndef SRNOS_BUZZER_QUEUE_CAPACITY
  #define SRNOS_BUZZER_QUEUE_CAPACITY 16
#endif

#if SRNOS_BUZZER_QUEUE_CAPACITY < 2
  #error "SRNOS_BUZZER_QUEUE_CAPACITY must be at least 2 because one ring slot is reserved"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SRNOS_BUZZER_STATUS_OK = 0,
    SRNOS_BUZZER_STATUS_NOT_INIT,
    SRNOS_BUZZER_STATUS_INVALID_ARG,
    SRNOS_BUZZER_STATUS_QUEUE_FULL,
    SRNOS_BUZZER_STATUS_HW_ERROR,
} SRNOS_BuzzerStatus;

typedef struct {
    uint32_t freq_hz;   /* 0 = rest (silence) */
    uint16_t dur_ms;    /* duration in milliseconds */
} SRNOS_BuzzerNote;

SRNOS_BuzzerStatus SRNOS_BuzzerStart(uint32_t freq_hz);
SRNOS_BuzzerStatus SRNOS_BuzzerStop(void);
SRNOS_BuzzerStatus SRNOS_BuzzerSetFrequency(uint32_t freq_hz);
SRNOS_BuzzerStatus SRNOS_BuzzerSetRefA4(float hz);
SRNOS_BuzzerStatus SRNOS_BuzzerStartNote(const char *note);

SRNOS_BuzzerStatus SRNOS_BuzzerEnqueue(SRNOS_BuzzerNote n);
SRNOS_BuzzerStatus SRNOS_BuzzerEnqueueMany(const SRNOS_BuzzerNote *arr, uint16_t n);
SRNOS_BuzzerStatus SRNOS_BuzzerFlush(void);

SRNOS_BuzzerStatus SRNOS_BuzzerEnqueueFromISR(SRNOS_BuzzerNote n);
SRNOS_BuzzerStatus SRNOS_BuzzerFlushFromISR(void);

bool SRNOS_BuzzerIsActive(void);
bool SRNOS_BuzzerQueueEmpty(void);

#ifdef __cplusplus
}
#endif

#endif /* SRNOS_BUZZER_H */
