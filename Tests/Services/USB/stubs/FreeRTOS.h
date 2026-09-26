#ifndef TEST_FREERTOS_H
#define TEST_FREERTOS_H

#include <stdint.h>

typedef uint32_t UBaseType_t;

#define pdTRUE  1
#define pdFALSE 0

#define taskENTER_CRITICAL()             do { } while (0)
#define taskEXIT_CRITICAL()              do { } while (0)
#define taskENTER_CRITICAL_FROM_ISR()    ((UBaseType_t)0u)
#define taskEXIT_CRITICAL_FROM_ISR(x)    do { (void)(x); } while (0)

#endif
