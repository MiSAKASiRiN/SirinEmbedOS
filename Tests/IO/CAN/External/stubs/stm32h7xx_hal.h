#pragma once
#include <stdint.h>
typedef struct { int unused; } SPI_HandleTypeDef;
typedef struct { int unused; } DMA_HandleTypeDef;
typedef struct { int unused; } FDCAN_HandleTypeDef;
typedef struct { int unused; } GPIO_TypeDef;
extern uint32_t SRNOS_TestPrimask, SRNOS_TestIpsr;
void SRNOS_TestUnmask(void);
static inline uint32_t __get_PRIMASK(void) { return SRNOS_TestPrimask; }
static inline uint32_t __get_IPSR(void) { return SRNOS_TestIpsr; }
static inline void __disable_irq(void) { SRNOS_TestPrimask=1; }
static inline void __set_PRIMASK(uint32_t key) {
    uint32_t previous=SRNOS_TestPrimask;SRNOS_TestPrimask=key;
    if(previous&&!key)SRNOS_TestUnmask();
}
static inline void __enable_irq(void) { __set_PRIMASK(0); }
#define __NOP() ((void)0)
#ifdef __cplusplus
extern "C" {
#endif
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t);
#ifdef __cplusplus
}
#endif
