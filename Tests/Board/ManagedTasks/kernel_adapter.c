#include "kernel_adapter.h"
#include "cycle_internal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32h7xx.h"
#include "SirinEmbedOS/Diagnostics/Time/Time.h"
/* Test-only benchmark: each interrupt mask is limited to one hook pair.
 * Includes complete start/end calls, subtracts only two counter reads. */
void board_hook_benchmark(HookBenchmark *out) {
 SRNOS_CycleContext c;SRNOS_CycleAttach(&c,1);
 uint32_t minimum=UINT32_MAX,maximum=0;uint64_t sum=0;
 uint32_t mask=__get_PRIMASK();__disable_irq();
 uint32_t a=DWT->CYCCNT,b=DWT->CYCCNT;uint32_t overhead=b-a;__set_PRIMASK(mask);
 for(unsigned i=0;i<128;i++) {
  uint32_t release=xTaskGetTickCount();mask=__get_PRIMASK();__disable_irq();
  a=DWT->CYCCNT;SRNOS_CycleBegin(&c,release);SRNOS_CycleEnd(&c);b=DWT->CYCCNT;
  __set_PRIMASK(mask);uint32_t cost=b-a-overhead;
  if(cost<minimum)minimum=cost;if(cost>maximum)maximum=cost;sum+=cost;
  vTaskDelay(1);
 }
 *out=(HookBenchmark){SRNOS_TimeGetCpuHz(),minimum,(uint32_t)(sum/128),maximum,overhead};
}
void board_idle_counters(uint64_t *time,uint64_t *idle) {
 TaskStatus_t s;taskENTER_CRITICAL();
 *time=SRNOS_TimeGetUptimeCycles64();vTaskGetInfo(xTaskGetIdleTaskHandle(),&s,pdFALSE,eInvalid);
 *idle=s.ulRunTimeCounter;taskEXIT_CRITICAL();
}
