#include "clock_internal.h"
#include "Kernel/thread_internal.h"
#include <string.h>

static SRNOS_ClockSnapshot mapping;
static uint64_t fresh_until_us;

bool SRNOS_ClockGetSnapshot(SRNOS_ClockSnapshot *out)
{
    if (!out || !SRNOS_KernelTaskContext()) return false;
    taskENTER_CRITICAL();
    *out=mapping;
    out->uptime_us=SRNOS_TimeUs();
    if (out->absolute_valid) {
        if (out->uptime_us< mapping.uptime_us ||
            UINT64_MAX-mapping.unix_us<out->uptime_us-mapping.uptime_us) {
            out->absolute_valid=false; out->unix_us=0;
        } else {
            out->unix_us+=out->uptime_us-mapping.uptime_us;
            out->holdover=out->holdover || out->uptime_us>=fresh_until_us;
        }
    }
    taskEXIT_CRITICAL();
    return true;
}
bool SRNOS_ClockGetUnixUs(uint64_t *unix_us)
{
    if (!unix_us) return false;
    *unix_us=0;
    SRNOS_ClockSnapshot s;
    if (!SRNOS_ClockGetSnapshot(&s) || !s.absolute_valid) return false;
    *unix_us=s.unix_us;
    return true;
}
bool SRNOS_ClockConvertUptime(const SRNOS_ClockSnapshot *s,uint64_t uptime_us,uint64_t *unix_us)
{
    if (!unix_us) return false;
    *unix_us=0;
    if (!s || !s->absolute_valid || uptime_us>s->uptime_us ||
        s->uptime_us-uptime_us>s->unix_us) return false;
    *unix_us=s->unix_us-(s->uptime_us-uptime_us);
    return true;
}
void SRNOS_ClockSynchronize(uint64_t uptime_us,uint64_t unix_us,uint32_t rtt,
    uint8_t stratum,uint8_t leap,uint64_t fresh_for_us)
{
    taskENTER_CRITICAL();
    int64_t correction=0;
    if (mapping.absolute_valid)
        correction=(int64_t)unix_us-((int64_t)mapping.unix_us+
            (int64_t)(uptime_us-mapping.uptime_us));
    mapping=(SRNOS_ClockSnapshot){.uptime_us=uptime_us,.unix_us=unix_us,
        .last_sync_uptime_us=uptime_us,.last_correction_us=correction,
        .generation=mapping.generation+1u,.round_trip_us=rtt,.stratum=stratum,
        .leap_indicator=leap,.absolute_valid=true,.holdover=fresh_for_us==0};
    fresh_until_us=uptime_us+fresh_for_us;
    taskEXIT_CRITICAL();
}
void SRNOS_ClockHoldover(void)
{
    taskENTER_CRITICAL(); mapping.holdover=true; taskEXIT_CRITICAL();
}
