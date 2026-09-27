#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef struct { int State; } SPI_HandleTypeDef;
typedef struct { int State; } DMA_HandleTypeDef;
typedef struct { int unused; } I2C_HandleTypeDef;
typedef struct { int value; } GPIO_TypeDef;
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
#define I2C_MEMADD_SIZE_8BIT 1
#define GPIO_PIN_SET 1
#define GPIO_PIN_RESET 0
#define HAL_SPI_STATE_READY 0
#define HAL_SPI_STATE_BUSY 1
static inline uint32_t __get_PRIMASK(void) { return 0; }
static inline void __disable_irq(void) {}
static inline void __set_PRIMASK(uint32_t m) {(void)m;}
static inline uint32_t __get_IPSR(void) { return 0; }
uint32_t HAL_GetTick(void);
void HAL_GPIO_WritePin(GPIO_TypeDef *,uint16_t,int);
HAL_StatusTypeDef HAL_SPI_TransmitReceive_DMA(SPI_HandleTypeDef *,uint8_t *,uint8_t *,uint16_t);
static inline int HAL_SPI_GetState(SPI_HandleTypeDef *h) { return h->State; }
static inline int HAL_SPI_GetError(SPI_HandleTypeDef *h) { (void)h; return 0; }
