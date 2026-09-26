//
// drv_spi.c — BSP SPI adapter for MCP2518FD
//
#include "SirinEmbedOS/Devices/MCP2518FD/drv_spi.h"
#include "SirinEmbedOS/IO/SPI/bsp_spi.h"
#include "main.h"
#include "cmsis_os2.h"
#include "SirinEmbedOS/Diagnostics/Watch/omx_debug_watch.h"
#include <stddef.h>

#define MCP2518FD_SPI_TIMEOUT_MS   (5u)
#ifndef DRV_SPI_BUSY_TIMEOUT_MS
#define DRV_SPI_BUSY_TIMEOUT_MS    (2u)
#endif

typedef struct
{
    bsp_spi_device_t  dev;
    bsp_spi_bus_id_t  bus_id;
    GPIO_TypeDef     *cs_port;
    uint16_t          cs_pin;
    uint8_t           configured;
    uint8_t           ready;
} drv_spi_slot_t;

static drv_spi_slot_t s_slots[DRV_SPI_MAX_DEVICES];
static uint8_t s_dbg_initialized[DRV_SPI_MAX_DEVICES];

OMX_DEBUG_WATCH_BSS volatile omx_dbg_mcp2518fd_t g_omx_dbg_mcp2518fd[DRV_SPI_MAX_DEVICES];

static void drv_spi_dbg_init(CANFDSPI_MODULE_ID index)
{
    if ((index >= DRV_SPI_MAX_DEVICES) || (s_dbg_initialized[index] != 0u))
    {
        return;
    }

    omx_dbg_zero(&g_omx_dbg_mcp2518fd[index], sizeof(g_omx_dbg_mcp2518fd[index]));
    omx_dbg_init(&g_omx_dbg_mcp2518fd[index].h,
                 OMX_DBG_MODULE_MCP2518FD,
                 (uint16_t)index,
                 (uint16_t)sizeof(g_omx_dbg_mcp2518fd[index]));
    s_dbg_initialized[index] = 1u;
}

static void drv_spi_yield(void)
{
    if (osKernelGetState() == osKernelRunning)
    {
        osDelay(1);
    }
    else
    {
        __NOP();
    }
}

int8_t DRV_SPI_AttachDevice(CANFDSPI_MODULE_ID index,
                            bsp_spi_bus_id_t bus_id,
                            GPIO_TypeDef *cs_port,
                            uint16_t cs_pin)
{
    if (index >= DRV_SPI_MAX_DEVICES || !cs_port)
        return -1;

    drv_spi_dbg_init(index);

    drv_spi_slot_t *slot = &s_slots[index];
    slot->bus_id = bus_id;
    slot->cs_port = cs_port;
    slot->cs_pin = cs_pin;
    slot->configured = 1u;
    slot->ready = 0u;

    return 0;
}

static bsp_spi_status_t drv_spi_prepare(CANFDSPI_MODULE_ID index)
{
    if (index >= DRV_SPI_MAX_DEVICES)
        return BSP_SPI_INVALID_PARAM;

    drv_spi_slot_t *slot = &s_slots[index];

    if (!slot->configured)
    {
        if (index == 0u)
        {
            slot->bus_id = BSP_SPI_BUS_4;
            slot->cs_port = MCP_CS_GPIO_Port;
            slot->cs_pin = MCP_CS_Pin;
            slot->configured = 1u;
        }
        else
        {
            return BSP_SPI_INVALID_PARAM;
        }
    }

    if (slot->ready)
        return BSP_SPI_OK;

    bsp_spi_status_t st = bsp_spi_device_init(&slot->dev,
                                              slot->bus_id,
                                              slot->cs_port,
                                              slot->cs_pin);
    if (st == BSP_SPI_OK)
    {
        slot->ready = 1u;
    }

    return st;
}

int8_t DRV_SPI_TransferData(CANFDSPI_MODULE_ID index,
                            uint8_t *spiTxData,
                            uint8_t *spiRxData,
                            uint16_t spiTransferSize)
{
    volatile omx_dbg_mcp2518fd_t *dbg = NULL;
    if (index < DRV_SPI_MAX_DEVICES)
    {
        drv_spi_dbg_init(index);
        dbg = &g_omx_dbg_mcp2518fd[index];
    }

    if (dbg)
    {
        dbg->xfer_calls++;
        dbg->last_index = index;
        dbg->last_size = spiTransferSize;
        omx_dbg_end(&dbg->h, HAL_GetTick(), BSP_SPI_BUSY);
    }

    if (!spiTxData || !spiRxData || (spiTransferSize == 0u))
    {
        if (dbg)
        {
            dbg->xfer_error++;
            dbg->last_ret = (uint32_t)-1;
            omx_dbg_end(&dbg->h, HAL_GetTick(), BSP_SPI_INVALID_PARAM);
        }
        return -1;
    }

    if (drv_spi_prepare(index) != BSP_SPI_OK)
    {
        if (dbg)
        {
            dbg->xfer_error++;
            dbg->last_ret = (uint32_t)-1;
            omx_dbg_end(&dbg->h, HAL_GetTick(), BSP_SPI_ERROR);
        }
        return -1;
    }

    drv_spi_slot_t *slot = &s_slots[index];
    const uint32_t start = HAL_GetTick();

    for (;;)
    {
        bsp_spi_status_t st = bsp_spi_transfer_blocking(&slot->dev,
                                                        spiTxData,
                                                        spiRxData,
                                                        (size_t)spiTransferSize,
                                                        MCP2518FD_SPI_TIMEOUT_MS);
        if (st == BSP_SPI_OK)
        {
            if (dbg)
            {
                dbg->xfer_ok++;
                dbg->last_ret = 0u;
                omx_dbg_end(&dbg->h, HAL_GetTick(), BSP_SPI_OK);
            }
            return 0;
        }
        if (st != BSP_SPI_BUSY)
        {
            if (dbg)
            {
                dbg->xfer_error++;
                dbg->last_ret = (uint32_t)-1;
                omx_dbg_end(&dbg->h, HAL_GetTick(), (uint32_t)st);
            }
            return -1;
        }
        if ((HAL_GetTick() - start) > DRV_SPI_BUSY_TIMEOUT_MS)
        {
            if (dbg)
            {
                dbg->xfer_timeout++;
                dbg->last_ret = (uint32_t)-2;
                omx_dbg_end(&dbg->h, HAL_GetTick(), BSP_SPI_BUSY);
            }
            return -2;
        }
        if (dbg) dbg->xfer_busy++;
        drv_spi_yield();
    }
}
