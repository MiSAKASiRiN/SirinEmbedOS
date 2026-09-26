//
// Created by sirin on 2025/11/30.
//

#ifndef H723VG_V2_FREERTOS_LIB_24C02_H
#define H723VG_V2_FREERTOS_LIB_24C02_H

#include "SirinEmbedOS/IO/I2C/bsp_i2c.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 24C02 EEPROM basic parameters.
 */
#define LIB_24C02_SIZE_BYTES       (256U)  /* 2 Kbit = 256 bytes */
#define LIB_24C02_PAGE_SIZE        (8U)    /* Typical page size is 8 bytes */
#define LIB_24C02_WRITE_CYCLE_MS   (5U)    /* Typical internal write cycle */

/* Legacy aliases kept so existing code does not need a rename. */
#define EEPROM24C02_SIZE_BYTES     LIB_24C02_SIZE_BYTES
#define EEPROM24C02_PAGE_SIZE      LIB_24C02_PAGE_SIZE

/**
 * @brief Async operation type.
 */
typedef enum
{
    LIB_24C02_OP_NONE = 0,
    LIB_24C02_OP_READ,
    LIB_24C02_OP_WRITE
} lib_24c02_op_t;

/* Forward declaration */
struct lib_24c02;

/**
 * @brief Async callback type for 24C02 operations.
 *
 * @param eeprom   EEPROM handle.
 * @param user_ctx User context pointer from start API.
 * @param status   Final status.
 */
typedef void (*lib_24c02_async_cb_t)(struct lib_24c02 *eeprom,
                                     void *user_ctx,
                                     bsp_i2c_status_t status);

/**
 * @brief 24C02 EEPROM handle (async version).
 */
typedef struct lib_24c02
{
    bsp_i2c_device_t      i2c_dev;      /* Underlying I2C device.          */
    uint16_t              size_bytes;   /* Total size (default 256).       */
    uint8_t               page_size;    /* Page size (default 8).          */

    /* Async context */
    volatile lib_24c02_op_t   op;       /* Current operation type.         */
    volatile uint8_t          busy;     /* 0 = idle, 1 = busy.             */

    uint8_t              mem_addr;     /* Start address for this op.      */
    uint8_t             *rx_buf;       /* For read.                       */
    const uint8_t       *tx_buf;       /* For write.                      */
    size_t               len;          /* Length for this op.             */

    lib_24c02_async_cb_t user_cb;      /* User callback.                  */
    void                *user_ctx;     /* User context.                   */
} lib_24c02_t;

/**
 * @brief Initialize a 24C02 device on given I2C bus and address.
 *
 * @param eeprom        EEPROM handle.
 * @param bus_id        I2C bus ID (e.g. BSP_I2C_BUS_4).
 * @param i2c_addr_7bit 7-bit I2C address (usually 0x50 ~ 0x57).
 */
bsp_i2c_status_t lib_24c02_init(lib_24c02_t *eeprom,
                                bsp_i2c_bus_id_t bus_id,
                                uint16_t i2c_addr_7bit);

/**
 * @brief Start an async EEPROM read.
 *
 * This is a single I2C memory transaction. The driver checks the 24C02 address
 * range, but the caller must keep rx_buf valid until callback.
 *
 * @param eeprom   EEPROM handle.
 * @param mem_addr Start address inside EEPROM (0~255).
 * @param buf      RX buffer.
 * @param len      Number of bytes to read.
 * @param cb       Callback when finished.
 * @param ctx      User context pointer.
 */
bsp_i2c_status_t lib_24c02_read_async(lib_24c02_t *eeprom,
                                      uint8_t mem_addr,
                                      uint8_t *buf,
                                      size_t len,
                                      lib_24c02_async_cb_t cb,
                                      void *ctx);

/**
 * @brief Start an async EEPROM write (single-page).
 *
 * This API intentionally keeps one async transaction per call. Write must not
 * cross an EEPROM page boundary; use the blocking API when automatic page split
 * and write-cycle pacing are needed.
 *
 * @param eeprom   EEPROM handle.
 * @param mem_addr Start address inside EEPROM (0~255).
 * @param buf      TX buffer.
 * @param len      Number of bytes to write.
 * @param cb       Callback when finished.
 * @param ctx      User context pointer.
 */
bsp_i2c_status_t lib_24c02_write_async(lib_24c02_t *eeprom,
                                       uint8_t mem_addr,
                                       const uint8_t *buf,
                                       size_t len,
                                       lib_24c02_async_cb_t cb,
                                       void *ctx);

/**
 * @brief Check if this EEPROM is busy.
 */
static inline uint8_t lib_24c02_is_busy(const lib_24c02_t *eeprom)
{
    return (eeprom != NULL) ? eeprom->busy : 0U;
}

#ifdef __cplusplus
}
#endif

#endif // H723VG_V2_FREERTOS_LIB_24C02_H
