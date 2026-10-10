#pragma once
#include <stdint.h>
typedef struct {uint32_t CTRL,CYCCNT;} DWT_Type;
typedef struct {uint32_t DEMCR;} CoreDebug_Type;
extern DWT_Type dwt;
extern CoreDebug_Type debug;
extern uint32_t mask,SystemCoreClock;
#define DWT (&dwt)
#define CoreDebug (&debug)
#define CoreDebug_DEMCR_TRCENA_Msk 1u
#define DWT_CTRL_CYCCNTENA_Msk 1u
#define DWT_CTRL_NOCYCCNT_Msk 2u
static inline uint32_t __get_PRIMASK(void){return mask;}
static inline void __set_PRIMASK(uint32_t m){mask=m;}
static inline void __disable_irq(void){mask=1;}
static inline void __DMB(void){}
static inline void __NOP(void){}
static inline void SystemCoreClockUpdate(void){}
static inline uint32_t HAL_GetTick(void){return 0;}
static inline void HAL_Delay(uint32_t ms){(void)ms;}
