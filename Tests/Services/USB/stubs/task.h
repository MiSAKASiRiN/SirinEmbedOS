#ifndef TEST_TASK_H
#define TEST_TASK_H

#include "FreeRTOS.h"

static inline int xPortIsInsideInterrupt(void)
{
    return pdFALSE;
}

#endif
