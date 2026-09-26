//
// Blocking 24C02 EEPROM driver built on top of bsp_i2c.
//

#ifndef H723VG_V2_FREERTOS_LIB_24C02_BL_H
#define H723VG_V2_FREERTOS_LIB_24C02_BL_H

#include "SirinEmbedOS/IO/I2C/bsp_i2c.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Basic 24C02 parameters.
 */
#define LIB_24C02_BL_SIZE_BYTES   (256U)  /* 2 Kbit = 256 bytes */
#define LIB_24C02_BL_PAGE_SIZE    (8U)    /* Typical AT24C02 page = 8 bytes */
#define LIB_24C02_BL_TWR_MS       (5U)

/**
 * @brief Return status for blocking 24C02 APIs.
 */
typedef enum
{
    LIB_24C02_BL_OK = 0,
    LIB_24C02_BL_ERROR,
    LIB_24C02_BL_INVALID_PARAM,
    LIB_24C02_BL_BUSY
} lib_24c02_bl_status_t;

/**
 * @brief Blocking 24C02 handle.
 *
 * This driver is fully blocking from caller perspective:
 *  - Uses bsp_i2c async memory APIs plus bsp_i2c_wait_complete()
 *  - Does internal page-split for write
 *  - Uses light ACK polling for tWR (internal write cycle)
 */
typedef struct
{
    bsp_i2c_device_t   i2c_dev;     /* Underlying BSP I2C device        */
    uint16_t           dev_addr_7b; /* 7-bit I2C address (e.g. 0x50)    */
    uint16_t           size_bytes;  /* Total size in bytes (default 256)*/
    uint8_t            page_size;   /* Page size in bytes (default 8)   */
    volatile uint8_t   busy;        /* Same-handle blocking guard       */
} lib_24c02_bl_t;

/**
 * @brief Initialize blocking 24C02 driver.
 *
 * @param eeprom      Handle pointer.
 * @param bus_id      BSP I2C bus ID (e.g. BSP_I2C_BUS_4).
 * @param addr_7bit   7-bit EEPROM address (0x50 if A2..A0=0).
 *
 * @return LIB_24C02_BL_OK or error.
 */
lib_24c02_bl_status_t lib_24c02_bl_init(lib_24c02_bl_t *eeprom,
                                        bsp_i2c_bus_id_t bus_id,
                                        uint16_t addr_7bit);

/**
 * @brief Blocking read from EEPROM.
 *
 * Can cross page boundaries. Internally this is one bsp_i2c memory read.
 *
 * @param eeprom    EEPROM handle.
 * @param mem_addr  Start byte address [0..255].
 * @param buf       Output buffer.
 * @param len       Number of bytes to read.
 * @param timeout   HAL timeout in ms.
 *
 * @return LIB_24C02_BL_OK / LIB_24C02_BL_ERROR / LIB_24C02_BL_INVALID_PARAM
 */
lib_24c02_bl_status_t lib_24c02_bl_read(lib_24c02_bl_t *eeprom,
                                        uint8_t mem_addr,
                                        uint8_t *buf,
                                        size_t len,
                                        uint32_t timeout);

/**
 * @brief Blocking write to EEPROM with automatic page split.
 *
 * This function:
 *  - Splits the write region by 8-byte page boundary
 *  - For each chunk, calls bsp_i2c_mem_write() and waits for completion
 *  - After each page write, polls device ACK until ready or twr_ms expires
 *
 * @param eeprom    EEPROM handle.
 * @param mem_addr  Start byte address [0..255].
 * @param buf       Input buffer.
 * @param len       Number of bytes to write.
 * @param timeout   HAL timeout for each page transaction.
 * @param twr_ms    Max internal write cycle wait (e.g. 5 ms, 0 = skip).
 *
 * @return LIB_24C02_BL_OK / LIB_24C02_BL_ERROR / LIB_24C02_BL_INVALID_PARAM
 */
lib_24c02_bl_status_t lib_24c02_bl_write(lib_24c02_bl_t *eeprom,
                                         uint8_t mem_addr,
                                         const uint8_t *buf,
                                         size_t len,
                                         uint32_t timeout,
                                         uint32_t twr_ms);

/**
 * @brief Convenience: read 1 byte.
 */
static inline lib_24c02_bl_status_t lib_24c02_bl_read_byte(lib_24c02_bl_t *eeprom,
                                                           uint8_t mem_addr,
                                                           uint8_t *value,
                                                           uint32_t timeout)
{
    return lib_24c02_bl_read(eeprom, mem_addr, value, 1U, timeout);
}

/**
 * @brief Convenience: write 1 byte.
 */
static inline lib_24c02_bl_status_t lib_24c02_bl_write_byte(lib_24c02_bl_t *eeprom,
                                                            uint8_t mem_addr,
                                                            uint8_t value,
                                                            uint32_t timeout,
                                                            uint32_t twr_ms)
{
    return lib_24c02_bl_write(eeprom, mem_addr, &value, 1U, timeout, twr_ms);
}

#ifdef __cplusplus
}
#endif

#endif // H723VG_V2_FREERTOS_LIB_24C02_BL_H
