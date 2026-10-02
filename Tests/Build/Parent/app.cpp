#include "SirinEmbedOS/Kernel/Startup.h"
#include "SirinEmbedOS/Config.h"
#include "SirinEmbedOS/Kernel/Thread.h"
#include <cerrno>
static_assert(SRNOS_APP_STACK_BYTES == 8192);
thread_local volatile unsigned tls_initialized = 0x12345678;
thread_local volatile unsigned tls_zero;
volatile unsigned observed;
struct ConstructorCheck { ConstructorCheck() { errno = 7; tls_zero = 1; } } boot_check;
static SRNOS_StepResult step(void *, const SRNOS_ThreadWake *) {
  observed = tls_initialized + tls_zero + errno;
  ++tls_zero;
  return SRNOS_STEP_CONTINUE;
}
volatile SRNOS_Result create_result;
void SRNOS_ApplicationMain() {
  observed = tls_initialized + tls_zero + errno;
  SRNOS_ThreadConfig config{};
  config.name="ParentWorker";config.step=step;config.stack_bytes=2048;
  config.priority=24;config.mode=SRNOS_TASK_PERIODIC;config.period_ms=1;
  SRNOS_Thread task;
  create_result=SRNOS_ThreadCreate(&config,&task);
}
