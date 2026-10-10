#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
extern uint32_t SRNOS_TestFDMask, SRNOS_TestFDException;
uint32_t HAL_GetTick(void);
static inline uint32_t __get_IPSR(void) { return SRNOS_TestFDException; }
static inline uint32_t __get_BASEPRI(void) { return SRNOS_TestFDMask; }
static inline void __set_BASEPRI(uint32_t value) { SRNOS_TestFDMask = value; }
static inline void __set_BASEPRI_MAX(uint32_t value) {
    if (value && (!SRNOS_TestFDMask || value < SRNOS_TestFDMask)) SRNOS_TestFDMask = value;
}
static inline void __DSB(void) {}
static inline void __ISB(void) {}
static inline void __DMB(void) {}
#ifdef __cplusplus
}
#endif
