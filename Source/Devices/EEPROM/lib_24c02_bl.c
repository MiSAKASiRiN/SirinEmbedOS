//
// Created by sirin on 2025/12/1.
//
//
// Blocking 24C02 EEPROM driver implementation
//

#include "SirinEmbedOS/Devices/EEPROM/lib_24c02_bl.h"
#include "cmsis_os2.h"

#define LIB_24C02_BL_POLL_STEP_MS          1u
#define LIB_24C02_BL_READY_XFER_TIMEOUT_MS 2u

/* ----------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------- */

/**
 * @brief Simple range check.
 */
static lib_24c02_bl_status_t lib_24c02_bl_check_range(const lib_24c02_bl_t *eeprom,
                                                      uint8_t mem_addr,
                                                      size_t len)
{
    if (eeprom == NULL)
    {
        return LIB_24C02_BL_INVALID_PARAM;
    }

    if (len == 0U)
    {
        return LIB_24C02_BL_INVALID_PARAM;
    }

    uint32_t end_addr = (uint32_t)mem_addr + (uint32_t)len;
    if (end_addr > eeprom->size_bytes)
    {
        /* Out of range */
        return LIB_24C02_BL_INVALID_PARAM;
    }

    return LIB_24C02_BL_OK;
}

static lib_24c02_bl_status_t lib_24c02_bl_from_i2c_status(bsp_i2c_status_t st)
{
    switch (st)
    {
    case BSP_I2C_OK:
        return LIB_24C02_BL_OK;
    case BSP_I2C_BUSY:
        return LIB_24C02_BL_BUSY;
    case BSP_I2C_INVALID_PARAM:
        return LIB_24C02_BL_INVALID_PARAM;
    case BSP_I2C_ERROR:
    default:
        return LIB_24C02_BL_ERROR;
    }
}

static uint32_t lib_24c02_bl_ms_to_ticks(uint32_t ms)
{
    if (ms == 0U)
    {
        return 0U;
    }

    uint32_t hz = osKernelGetTickFreq();
    uint64_t num = (uint64_t)ms * (uint64_t)hz + 999ULL;
    uint32_t ticks = (uint32_t)(num / 1000ULL);
    return (ticks == 0U) ? 1U : ticks;
}

static void lib_24c02_bl_delay_ms(uint32_t ms)
{
    if (ms == 0U)
    {
        return;
    }

    if ((__get_IPSR() == 0U) && (osKernelGetState() == osKernelRunning))
    {
        osDelay(lib_24c02_bl_ms_to_ticks(ms));
    }
    else
    {
        HAL_Delay(ms);
    }
}

static uint8_t lib_24c02_bl_try_lock(lib_24c02_bl_t *eeprom)
{
    if ((eeprom == NULL) || (__get_IPSR() != 0U))
    {
        return 0U;
    }

    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (eeprom->busy != 0U)
    {
        if (primask == 0U)
        {
            __enable_irq();
        }
        return 0U;
    }

    eeprom->busy = 1U;
    if (primask == 0U)
    {
        __enable_irq();
    }
    return 1U;
}

static void lib_24c02_bl_unlock(lib_24c02_bl_t *eeprom)
{
    if (eeprom == NULL)
    {
        return;
    }

    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    eeprom->busy = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }
}

static uint8_t lib_24c02_bl_elapsed(uint32_t start, uint32_t timeout_ms)
{
    if (timeout_ms == 0U)
    {
        return 1U;
    }
    return ((HAL_GetTick() - start) >= timeout_ms) ? 1U : 0U;
}

static bsp_i2c_status_t lib_24c02_bl_start_mem_read(lib_24c02_bl_t *eeprom,
                                                    uint8_t mem_addr,
                                                    uint8_t *buf,
                                                    size_t len,
                                                    uint32_t timeout)
{
    uint32_t start = HAL_GetTick();

    for (;;)
    {
        bsp_i2c_status_t ist = bsp_i2c_mem_read(&eeprom->i2c_dev,
                                                mem_addr,
                                                I2C_MEMADD_SIZE_8BIT,
                                                buf,
                                                len,
                                                NULL,
                                                NULL);
        if (ist != BSP_I2C_BUSY)
        {
            return ist;
        }
        if (lib_24c02_bl_elapsed(start, timeout) != 0U)
        {
            return BSP_I2C_BUSY;
        }
        lib_24c02_bl_delay_ms(LIB_24C02_BL_POLL_STEP_MS);
    }
}

static bsp_i2c_status_t lib_24c02_bl_start_mem_write(lib_24c02_bl_t *eeprom,
                                                     uint8_t mem_addr,
                                                     const uint8_t *buf,
                                                     size_t len,
                                                     uint32_t timeout)
{
    uint32_t start = HAL_GetTick();

    for (;;)
    {
        bsp_i2c_status_t ist = bsp_i2c_mem_write(&eeprom->i2c_dev,
                                                 mem_addr,
                                                 I2C_MEMADD_SIZE_8BIT,
                                                 buf,
                                                 len,
                                                 NULL,
                                                 NULL);
        if (ist != BSP_I2C_BUSY)
        {
            return ist;
        }
        if (lib_24c02_bl_elapsed(start, timeout) != 0U)
        {
            return BSP_I2C_BUSY;
        }
        lib_24c02_bl_delay_ms(LIB_24C02_BL_POLL_STEP_MS);
    }
}

static uint32_t lib_24c02_bl_poll_xfer_timeout(uint32_t timeout)
{
    if ((timeout == 0U) || (timeout > LIB_24C02_BL_READY_XFER_TIMEOUT_MS))
    {
        return LIB_24C02_BL_READY_XFER_TIMEOUT_MS;
    }
    return timeout;
}

static lib_24c02_bl_status_t lib_24c02_bl_wait_ready(lib_24c02_bl_t *eeprom,
                                                     uint8_t probe_addr,
                                                     uint32_t timeout,
                                                     uint32_t twr_ms)
{
    (void)probe_addr;

    if (twr_ms == 0U)
    {
        return LIB_24C02_BL_OK;
    }

    uint32_t start = HAL_GetTick();
    uint32_t poll_timeout = lib_24c02_bl_poll_xfer_timeout(timeout);

    for (;;)
    {
        bsp_i2c_status_t ist = bsp_i2c_device_ready(&eeprom->i2c_dev,
                                                    1U,
                                                    poll_timeout);
        if (ist == BSP_I2C_OK)
        {
            return LIB_24C02_BL_OK;
        }
        else if (ist == BSP_I2C_INVALID_PARAM)
        {
            return LIB_24C02_BL_INVALID_PARAM;
        }

        if (lib_24c02_bl_elapsed(start, twr_ms) != 0U)
        {
            return LIB_24C02_BL_BUSY;
        }
        lib_24c02_bl_delay_ms(LIB_24C02_BL_POLL_STEP_MS);
    }
}

/* ----------------------------------------------------------------------
 * Public APIs
 * -------------------------------------------------------------------- */

lib_24c02_bl_status_t lib_24c02_bl_init(lib_24c02_bl_t *eeprom,
                                        bsp_i2c_bus_id_t bus_id,
                                        uint16_t addr_7bit)
{
    if (eeprom == NULL)
    {
        return LIB_24C02_BL_INVALID_PARAM;
    }

    bsp_i2c_status_t st = bsp_i2c_device_init(&eeprom->i2c_dev, bus_id, addr_7bit);
    if (st != BSP_I2C_OK)
    {
        return lib_24c02_bl_from_i2c_status(st);
    }

    eeprom->dev_addr_7b = (uint16_t)(addr_7bit & 0x7FU);
    eeprom->size_bytes  = LIB_24C02_BL_SIZE_BYTES;
    eeprom->page_size   = LIB_24C02_BL_PAGE_SIZE;
    eeprom->busy        = 0U;

    return LIB_24C02_BL_OK;
}

lib_24c02_bl_status_t lib_24c02_bl_read(lib_24c02_bl_t *eeprom,
                                        uint8_t mem_addr,
                                        uint8_t *buf,
                                        size_t len,
                                        uint32_t timeout)
{
    if (eeprom == NULL || buf == NULL)
    {
        return LIB_24C02_BL_INVALID_PARAM;
    }

    lib_24c02_bl_status_t st = lib_24c02_bl_check_range(eeprom, mem_addr, len);
    if (st != LIB_24C02_BL_OK)
    {
        return st;
    }

    if (lib_24c02_bl_try_lock(eeprom) == 0U)
    {
        return LIB_24C02_BL_BUSY;
    }

    bsp_i2c_status_t ist = lib_24c02_bl_start_mem_read(eeprom,
                                                       mem_addr,
                                                       buf,
                                                       len,
                                                       timeout);
    if (ist == BSP_I2C_OK)
    {
        ist = bsp_i2c_wait_complete(&eeprom->i2c_dev, timeout);
    }

    st = lib_24c02_bl_from_i2c_status(ist);
    lib_24c02_bl_unlock(eeprom);
    return st;
}

lib_24c02_bl_status_t lib_24c02_bl_write(lib_24c02_bl_t *eeprom,
                                         uint8_t mem_addr,
                                         const uint8_t *buf,
                                         size_t len,
                                         uint32_t timeout,
                                         uint32_t twr_ms)
{
    if (eeprom == NULL || buf == NULL)
    {
        return LIB_24C02_BL_INVALID_PARAM;
    }

    lib_24c02_bl_status_t st = lib_24c02_bl_check_range(eeprom, mem_addr, len);
    if (st != LIB_24C02_BL_OK)
    {
        return st;
    }

    if (lib_24c02_bl_try_lock(eeprom) == 0U)
    {
        return LIB_24C02_BL_BUSY;
    }

    uint16_t size_bytes = eeprom->size_bytes;
    uint8_t  page_size  = eeprom->page_size;
    if (page_size == 0U)
    {
        lib_24c02_bl_unlock(eeprom);
        return LIB_24C02_BL_INVALID_PARAM;
    }

    uint8_t        addr      = mem_addr;
    const uint8_t *p         = buf;
    size_t         remaining = len;

    while (remaining > 0U)
    {
        uint32_t addr32 = (uint32_t)addr;
        if (addr32 >= size_bytes)
        {
            lib_24c02_bl_unlock(eeprom);
            return LIB_24C02_BL_INVALID_PARAM;
        }

        /* Calculate how many bytes can be written in this page. */
        uint8_t page_offset = (uint8_t)(addr % page_size);
        uint8_t page_remain = (uint8_t)(page_size - page_offset);

        size_t chunk = remaining;
        if (chunk > page_remain)
        {
            chunk = page_remain;
        }

        bsp_i2c_status_t ist = lib_24c02_bl_start_mem_write(eeprom,
                                                            addr,
                                                            p,
                                                            chunk,
                                                            timeout);
        if (ist == BSP_I2C_OK)
        {
            ist = bsp_i2c_wait_complete(&eeprom->i2c_dev, timeout);
        }
        if (ist != BSP_I2C_OK)
        {
            st = lib_24c02_bl_from_i2c_status(ist);
            lib_24c02_bl_unlock(eeprom);
            return st;
        }

        st = lib_24c02_bl_wait_ready(eeprom, addr, timeout, twr_ms);
        if (st != LIB_24C02_BL_OK)
        {
            lib_24c02_bl_unlock(eeprom);
            return st;
        }

        addr      = (uint8_t)(addr + (uint8_t)chunk);
        p        += chunk;
        remaining -= chunk;
    }

    lib_24c02_bl_unlock(eeprom);
    return LIB_24C02_BL_OK;
}
