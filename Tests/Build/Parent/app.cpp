#include "SirinEmbedOS/Kernel/Startup.h"
#include "SirinEmbedOSConfig.h"
#include "cmsis_os2.h"
#include <cerrno>
static_assert(SRNOS_APP_STACK_BYTES == 8192);
thread_local volatile unsigned tls_initialized = 0x12345678;
thread_local volatile unsigned tls_zero;
volatile unsigned observed;
struct ConstructorCheck { ConstructorCheck() { errno = 7; tls_zero = 1; } } boot_check;
void SRNOS_ApplicationMain() {
  observed = tls_initialized + tls_zero + errno;
  for (;;) { ++tls_zero; osDelay(1); }
}
