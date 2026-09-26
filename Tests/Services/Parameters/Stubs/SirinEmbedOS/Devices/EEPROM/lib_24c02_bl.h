#pragma once
#include <stdint.h>
#include <stddef.h>
#define LIB_24C02_BL_SIZE_BYTES 256U
typedef int bsp_i2c_bus_id_t;
typedef struct { int unused; } lib_24c02_bl_t;
typedef enum {LIB_24C02_BL_OK,LIB_24C02_BL_ERROR,LIB_24C02_BL_INVALID_PARAM,LIB_24C02_BL_BUSY} lib_24c02_bl_status_t;
#ifdef __cplusplus
extern "C" {
#endif
lib_24c02_bl_status_t lib_24c02_bl_init(lib_24c02_bl_t *,bsp_i2c_bus_id_t,uint16_t);
lib_24c02_bl_status_t lib_24c02_bl_read(lib_24c02_bl_t *,uint8_t,uint8_t *,size_t,uint32_t);
lib_24c02_bl_status_t lib_24c02_bl_write(lib_24c02_bl_t *,uint8_t,const uint8_t *,size_t,uint32_t,uint32_t);
#ifdef __cplusplus
}
#endif
