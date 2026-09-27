#pragma once
#include <stdint.h>
typedef enum { BUZZER_STATUS_OK, BUZZER_STATUS_HW_ERROR } BuzzerStatus;
BuzzerStatus Buzzer_Init(const void *);
BuzzerStatus Buzzer_Start(uint32_t);
BuzzerStatus Buzzer_Stop(void);
BuzzerStatus Buzzer_Service(void);
