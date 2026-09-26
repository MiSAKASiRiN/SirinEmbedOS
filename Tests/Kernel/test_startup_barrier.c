#include <assert.h>
#include <stdio.h>
#include "startup_barrier.h"

static int event_storage;
static uint32_t returned_flags;
static uint32_t expected_mask;
static unsigned calls;

/* Use the upstream CMSIS declarations/constants and fake only the RTOS wait. */
uint32_t osEventFlagsWait(osEventFlagsId_t event, uint32_t flags,
                         uint32_t options, uint32_t timeout)
{
    assert(event == &event_storage);
    assert(flags == expected_mask);
    assert(options == (osFlagsWaitAll | osFlagsNoClear));
    assert(timeout == osWaitForever);
    ++calls;
    return returned_flags;
}

static void check(uint32_t required, uint32_t result, bool ready)
{
    expected_mask = required;
    returned_flags = result;
    calls = 0;
    assert(SRNOS_StartupWaitReady(&event_storage, required) == ready);
    assert(calls == 1);
}

int main(void)
{
    /* Early CAN workers see 2, a late Indicators worker sees 3. Configured
     * application threads also see 3 after both milestones have completed. */
    check(2u, 2u, true);
    check(2u, 3u, true);
    check(1u, 1u, true);
    check(1u, 3u, true);
    check(3u, 3u, true);
    check(3u, 1u, false);
    check(3u, 2u, false);
    check(1u, 2u, false);
    check(2u, 1u, false);
    check(1u, 0u, false);
    check(2u, 0u, false);
    const uint32_t errors[] = {osFlagsErrorUnknown, osFlagsErrorTimeout,
        osFlagsErrorResource, osFlagsErrorParameter, osFlagsErrorISR,
        osFlagsError | 3u};
    for (unsigned i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        check(1u, errors[i], false);
        check(2u, errors[i], false);
    }
    puts("PASS: startup barriers accept combined readiness, reject missing bits/errors, retain wait-all/no-clear");
}
