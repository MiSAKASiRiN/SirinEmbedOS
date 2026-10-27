#ifndef SRNOS_SERVICES_CLOCK_H
#define SRNOS_SERVICES_CLOCK_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/** Coherent time pair. uptime_us has the SRNOS_TimeUs() epoch and never changes
 * on NTP updates. unix_us is UTC microseconds since 1970-01-01, valid only after
 * a successful NTP exchange in this boot. No RTC/EEPROM time restoration.
 * Absolute time may step in either direction on a new sync; generation changes
 * on every accepted update. Use uptime for deadlines and elapsed intervals.
 * holdover means the server is no longer freshly tracked (also true after an
 * intentionally one-shot sync). It does not invalidate the extrapolated time.
 * round_trip_us is measured software RTT, NOT a guaranteed clock error bound. */
typedef struct {
    uint64_t uptime_us, unix_us, last_sync_uptime_us;
    int64_t last_correction_us;
    uint32_t generation, round_trip_us;
    uint8_t stratum, leap_indicator;
    bool absolute_valid, holdover;
} SRNOS_ClockSnapshot;
/** Task-context, bounded/nonblocking copies. false means invalid argument or
 * context; ClockGetSnapshot succeeds before sync with absolute_valid=false.
 * ClockGetUnixUs returns false and writes zero until absolute time is valid. */
bool SRNOS_ClockGetSnapshot(SRNOS_ClockSnapshot *out);
bool SRNOS_ClockGetUnixUs(uint64_t *unix_us);
/** Convert a past SRNOS_TimeUs() timestamp using one captured clock mapping.
 * Rejects future timestamps/underflow/invalid mapping. A snapshot is not a
 * history of earlier corrections: keep it with the sample when required. */
bool SRNOS_ClockConvertUptime(const SRNOS_ClockSnapshot *clock,
    uint64_t uptime_us, uint64_t *unix_us);
#ifdef __cplusplus
}
#endif
#endif
