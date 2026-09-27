#pragma once
#include <stdint.h>
#include <stddef.h>
#define BSP_I2C_DMA_STAGING_SIZE 256U
#define BSP_I2C_MAX_TRANSFER_BYTES 64U
#define I2C_MEMADD_SIZE_16BIT 2U
#define BSP_I2C_BUS_4 0U
#define BSP_I2C_RECOVER_REASON_TIMEOUT 1U
#define BSP_I2C_RECOVER_REASON_USER 255U
typedef unsigned bsp_i2c_bus_id_t;
typedef enum { BSP_I2C_OK, BSP_I2C_ERROR, BSP_I2C_BUSY, BSP_I2C_INVALID_PARAM, BSP_I2C_TIMEOUT, BSP_I2C_CANCELLED } bsp_i2c_status_t;
typedef struct { volatile uint8_t pending; volatile bsp_i2c_status_t completed_status; } bsp_i2c_device_t;
typedef void (*bsp_i2c_callback_t)(bsp_i2c_device_t *,void *,bsp_i2c_status_t);
bsp_i2c_status_t bsp_i2c_device_init(bsp_i2c_device_t *,bsp_i2c_bus_id_t,uint16_t);
bsp_i2c_status_t bsp_i2c_mem_read(bsp_i2c_device_t *,uint16_t,uint16_t,uint8_t *,size_t,bsp_i2c_callback_t,void *);
bsp_i2c_status_t bsp_i2c_mem_write(bsp_i2c_device_t *,uint16_t,uint16_t,const uint8_t *,size_t,bsp_i2c_callback_t,void *);
bsp_i2c_status_t bsp_i2c_cancel_device(bsp_i2c_device_t *,uint32_t);
uint32_t HAL_GetTick(void);
uint32_t __get_PRIMASK(void);
uint32_t __get_BASEPRI(void);
uint32_t __get_IPSR(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t);
