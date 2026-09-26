#ifndef SRNOS_STARTUP_BARRIER_H
#define SRNOS_STARTUP_BARRIER_H
#include <stdbool.h>
#include "cmsis_os2.h"

/* CMSIS returns the group's flags, including unrelated ready bits. Errors
 * also contain low bits, so check osFlagsError before testing the mask. Keep
 * readiness latched for every worker, including ones scheduled much later. */
static inline bool SRNOS_StartupWaitReady(osEventFlagsId_t event, uint32_t required)
{
    const uint32_t flags = osEventFlagsWait(event, required,
                                          osFlagsWaitAll | osFlagsNoClear,
                                          osWaitForever);
    return (flags & osFlagsError) == 0u && (flags & required) == required;
}
#endif
