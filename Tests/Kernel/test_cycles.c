#include "cycle_internal.h"
#include "fake_runtime.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
 test_runtime_init();test_kernel_state=osKernelRunning;test_manual_clock=true;test_cycles=1000000;
 SRNOS_CycleConfigure(true);SRNOS_CycleTick(test_cycles,1000);
 SRNOS_CycleContext c;SRNOS_CycleAttach(&c,1);
 test_cycles=1000010;SRNOS_CycleBegin(&c,1000);test_cycles=1000110;SRNOS_CycleEnd(&c);
 test_cycles=1001040;SRNOS_CycleBegin(&c,1001);test_cycles=1001240;SRNOS_CycleEnd(&c);
 SRNOS_CycleSkipped(&c,2);test_cycles=1002000;
 SRNOS_MonitorCycle s;SRNOS_CycleSample(1,1,&s);
 assert(s.completed==2 && s.execution_min_us==100 && s.execution_mean_us==150 && s.execution_max_us==200);
 assert(s.start_late_mean_us==25 && s.start_late_max_us==40 && s.jitter_samples==1 && s.jitter_min_us==30 && s.jitter_max_us==30 && s.jitter_abs_mean_us==30);
 assert(s.skipped==2 && s.window_us==2000 && s.flags&SRNOS_CYCLE_PERIODIC);
 test_cycles=1004000;SRNOS_CycleBegin(&c,1003);test_cycles=1005100;SRNOS_CycleEnd(&c);SRNOS_CycleSample(1,1,&s);
 assert(s.completed==1 && s.deadline_misses==1 && s.execution_max_us==1100 && s.start_late_max_us==1000);
 test_identity_generation=2;SRNOS_CycleAttach(&c,0);test_cycles=1006000;SRNOS_CycleBegin(&c,0);test_cycles=1006300;SRNOS_CycleEnd(&c);
 SRNOS_CycleSample(1,1,&s);assert(!s.flags);SRNOS_CycleSample(1,2,&s);assert(s.completed==1 && s.execution_mean_us==300 && !(s.flags&(SRNOS_CYCLE_PERIODIC|SRNOS_CYCLE_JITTER_VALID)));
 test_cycles=UINT32_MAX-20u;SRNOS_CycleBegin(&c,0);test_cycles+=100;SRNOS_CycleEnd(&c);SRNOS_CycleSample(1,2,&s);assert(s.execution_max_us==100);
 test_identity_generation=3;test_cycles=5000000000ULL;SRNOS_CycleTick(test_cycles,5000000);SRNOS_CycleAttach(&c,1);
 SRNOS_CycleBegin(&c,5000000);test_cycles+=10;SRNOS_CycleEnd(&c);
 test_cycles+=980;SRNOS_CycleBegin(&c,5000001);test_cycles+=10;SRNOS_CycleEnd(&c);
 SRNOS_CycleSample(1,3,&s);assert(s.jitter_samples==1 && s.jitter_min_us==-10 && s.jitter_max_us==-10 && s.jitter_abs_mean_us==10);
 SRNOS_CycleSample(1,3,&s);assert(s.flags== (SRNOS_CYCLE_ACTIVE|SRNOS_CYCLE_PERIODIC) && !s.completed);
 /* A long sleeping task crosses multiple hardware wraps; per-tick trace
  * keeps its next execution and sample window on the same 64-bit clock. */
 test_cycles+=3ULL*(1ULL<<32);SRNOS_CycleTick(test_cycles,(uint32_t)(test_cycles/1000));
 SRNOS_CycleBegin(&c,(uint32_t)(test_cycles/1000));test_cycles+=123;SRNOS_CycleEnd(&c);
 SRNOS_CycleSample(1,3,&s);assert(s.execution_mean_us==123 && s.window_us==3ULL*(1ULL<<32)+123);
 SRNOS_CycleConfigure(false);test_cycles+=100;SRNOS_CycleAttach(&c,1);assert(!c.enabled);SRNOS_CycleSample(1,2,&s);assert(!s.flags);
 puts("PASS: cycle wall duration, actual window, signed jitter, release lateness, misses/skips, event validity, generation and counter wrap");
}
