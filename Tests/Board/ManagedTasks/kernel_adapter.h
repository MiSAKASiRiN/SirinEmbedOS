#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {uint32_t cpu_hz,minimum,mean,maximum,read_overhead;} HookBenchmark;
void board_hook_benchmark(HookBenchmark *out);
void board_idle_counters(uint64_t *time,uint64_t *idle);
#ifdef __cplusplus
}
#endif
