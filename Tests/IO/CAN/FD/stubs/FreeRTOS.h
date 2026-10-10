#pragma once
#include <stdint.h>
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 80u
#ifdef __cplusplus
extern "C" {
#endif
void SRNOS_TestFDCriticalEnter(void);
void SRNOS_TestFDCriticalExit(void);
#ifdef __cplusplus
}
#endif
#define taskENTER_CRITICAL() SRNOS_TestFDCriticalEnter()
#define taskEXIT_CRITICAL() SRNOS_TestFDCriticalExit()
