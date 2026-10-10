#include <assert.h>
#include <stdio.h>
#include "../../Source/Diagnostics/Time/Time.c"
DWT_Type dwt;
CoreDebug_Type debug;
uint32_t mask,SystemCoreClock=480000000;
int main(void) {
    SRNOS_TimeInit(0);
    uint64_t total=0;
    uint32_t rng=1;
    for(unsigned i=0;i<100000;++i) {
        rng=rng*1664525u+1013904223u;
        /* Large deltas cross many DWT and 64/32 quotient boundaries. */
        uint32_t delta=i%3?rng%10000:rng;
        dwt.CYCCNT+=delta;total+=delta;
        if(i%7==0)assert(SRNOS_TimeGetUptimeCycles64()==total);
        else assert(SRNOS_TimeGetUptimeUs()==total/480u);
        assert(!mask);
    }
    /* >32-bit elapsed since previous US query; cycles read keeps DWT extended. */
    for(unsigned i=0;i<5;++i) {
        dwt.CYCCNT+=UINT32_MAX;total+=UINT32_MAX;
        assert(SRNOS_TimeGetUptimeCycles64()==total);
    }
    assert(SRNOS_TimeGetUptimeUs()==total/480u);
    for(unsigned mhz=1;mhz<=550;++mhz) {
        SRNOS_TimeInit(mhz);assert(SRNOS_TimeGetUptimeUs()==total/mhz);
        for(unsigned i=0;i<7;++i) {
            dwt.CYCCNT+=mhz-1;total+=mhz-1;
            assert(SRNOS_TimeGetUptimeUs()==total/mhz);
        }
    }
    mask=1;assert(SRNOS_TimeGetUptimeUs()==total/550u && mask==1);
    puts("Time actual implementation: exact fractional carry, repeated DWT wraps, long gap, reinit/frequency and interrupt mask: PASS");
}
