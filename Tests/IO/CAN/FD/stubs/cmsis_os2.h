#pragma once
enum { osKernelInactive = 0, osKernelReady = 1, osKernelRunning = 2 };
#ifdef __cplusplus
extern "C" {
#endif
int osKernelGetState(void);
#ifdef __cplusplus
}
#endif
