#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef struct { int State; } SPI_HandleTypeDef;
typedef struct { int State; } DMA_HandleTypeDef;
typedef struct { int State; int ErrorCode; } I2C_HandleTypeDef;
typedef struct { int value; } GPIO_TypeDef;
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
#define I2C_MEMADD_SIZE_8BIT 1
#define I2C_MEMADD_SIZE_16BIT 2
extern uint32_t SRNOS_TestPrimask;
void SRNOS_TestPreempt(void);
static inline uint32_t __get_PRIMASK(void) { return SRNOS_TestPrimask; }
static inline void __disable_irq(void) { SRNOS_TestPrimask=1; }
static inline void __enable_irq(void) { SRNOS_TestPrimask=0; }
static inline void __set_PRIMASK(uint32_t m) { uint32_t old=SRNOS_TestPrimask;SRNOS_TestPrimask=m;if(old&&!m)SRNOS_TestPreempt(); }
static inline uint32_t __get_IPSR(void) { return 0; }
static inline uint32_t __get_BASEPRI(void) { return 0; }
static inline void __NOP(void) {}
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t);
static inline int HAL_I2C_GetState(I2C_HandleTypeDef *h) { return h->State; }
static inline int HAL_I2C_GetError(I2C_HandleTypeDef *h) { return h->ErrorCode; }
HAL_StatusTypeDef HAL_I2C_Master_Abort_IT(I2C_HandleTypeDef *,uint16_t);
HAL_StatusTypeDef HAL_I2C_Master_Transmit_DMA(I2C_HandleTypeDef *,uint16_t,uint8_t *,uint16_t);
HAL_StatusTypeDef HAL_I2C_Master_Receive_DMA(I2C_HandleTypeDef *,uint16_t,uint8_t *,uint16_t);
HAL_StatusTypeDef HAL_I2C_Mem_Write_DMA(I2C_HandleTypeDef *,uint16_t,uint16_t,uint16_t,uint8_t *,uint16_t);
HAL_StatusTypeDef HAL_I2C_Mem_Read_DMA(I2C_HandleTypeDef *,uint16_t,uint16_t,uint16_t,uint8_t *,uint16_t);
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *,uint16_t,uint32_t,uint32_t);
