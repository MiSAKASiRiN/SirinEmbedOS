#pragma once
#include <stdint.h>
#include <stddef.h>
#define SRNOS_I2C_DMA_STAGING_SIZE 256U
#define SRNOS_I2C_MAX_TRANSFER_BYTES 64U
#define I2C_MEMADD_SIZE_16BIT 2U
#define SRNOS_I2C_BUS_4 0U
#define SRNOS_I2C_RECOVER_REASON_TIMEOUT 1U
#define SRNOS_I2C_RECOVER_REASON_USER 255U
typedef unsigned SRNOS_I2CBusId;
typedef enum { SRNOS_I2C_OK, SRNOS_I2C_ERROR, SRNOS_I2C_BUSY, SRNOS_I2C_INVALID_PARAM, SRNOS_I2C_TIMEOUT, SRNOS_I2C_CANCELLED } SRNOS_I2CStatus;
typedef struct { volatile uint8_t pending; volatile SRNOS_I2CStatus completed_status; } SRNOS_I2CDevice;
typedef void (*SRNOS_I2CCallback)(SRNOS_I2CDevice *,void *,SRNOS_I2CStatus);
SRNOS_I2CStatus SRNOS_I2CDeviceInit(SRNOS_I2CDevice *,SRNOS_I2CBusId,uint16_t);
SRNOS_I2CStatus SRNOS_I2CMemRead(SRNOS_I2CDevice *,uint16_t,uint16_t,uint8_t *,size_t,SRNOS_I2CCallback,void *);
SRNOS_I2CStatus SRNOS_I2CMemWrite(SRNOS_I2CDevice *,uint16_t,uint16_t,const uint8_t *,size_t,SRNOS_I2CCallback,void *);
SRNOS_I2CStatus SRNOS_I2CCancelDevice(SRNOS_I2CDevice *,uint32_t);
uint32_t HAL_GetTick(void);
uint32_t __get_PRIMASK(void);
uint32_t __get_BASEPRI(void);
uint32_t __get_IPSR(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t);
