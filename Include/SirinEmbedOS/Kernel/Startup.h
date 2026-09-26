#ifndef SIRINEMBEDOS_KERNEL_STARTUP_H
#define SIRINEMBEDOS_KERNEL_STARTUP_H
#include "SirinEmbedOS/Kernel/System.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Implement exactly once in the application. Called in a normal thread after
 * HAL initialization and scheduler start, never from an ISR. May return (the SDK then deletes this initial application thread).
 * Application threads and their budgets are declared in ApplicationConfigure.
 * Architecture/registration changes are rejected once configuration freezes.
 * Do not implement another main() or initialize/start the scheduler again.
 */
void SRNOS_ApplicationMain(void);

#ifdef __cplusplus
}
#endif
#endif
