#pragma once
#include <stdint.h>
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)

typedef uint32_t TickType_t;
#define configTICK_RATE_HZ 1000u
