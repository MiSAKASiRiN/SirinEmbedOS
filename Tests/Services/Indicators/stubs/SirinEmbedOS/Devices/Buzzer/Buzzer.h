#pragma once
#include <stdint.h>
typedef enum { SRNOS_BUZZER_STATUS_OK, SRNOS_BUZZER_STATUS_HW_ERROR } SRNOS_BuzzerStatus;
SRNOS_BuzzerStatus SRNOS_BuzzerStart(uint32_t);
SRNOS_BuzzerStatus SRNOS_BuzzerStop(void);
SRNOS_BuzzerStatus SRNOS_BuzzerService(void);
