#ifndef SRNOS_CLOCK_INTERNAL_H
#define SRNOS_CLOCK_INTERNAL_H
#include "SirinEmbedOS/Services/Clock.h"
/* Network worker is the sole writer. No adjustment of the underlying clock. */
void SRNOS_ClockSynchronize(uint64_t uptime_us, uint64_t unix_us,
    uint32_t round_trip_us, uint8_t stratum, uint8_t leap, uint64_t fresh_for_us);
void SRNOS_ClockHoldover(void);
#endif
