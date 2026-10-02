//
// SPIAdapter.c — BSP SPI adapter for MCP2518FD
//
#include "Devices/MCP2518FD/spi_adapter_internal.h"
#include "IO/SPI/spi_internal.h"
#include "IO/can_board.h"
#include "cmsis_os2.h"
#include "SirinEmbedOS/Diagnostics/Watch/Watch.h"
#include <stddef.h>

#define SRNOS_MCP2518FD_SPI_TIMEOUT_MS   (5u)
#ifndef SRNOS_MCP2518FD_SPI_BUSY_TIMEOUT_MS
#define SRNOS_MCP2518FD_SPI_BUSY_TIMEOUT_MS    (2u)
#endif

typedef struct
{
    SRNOS_SPIDevice  dev;
    SRNOS_SPIBusId  bus_id;
    GPIO_TypeDef     *cs_port;
    uint16_t          cs_pin;
    uint8_t           configured;
    uint8_t           ready;
} spi_adapter_slot_t;

static spi_adapter_slot_t s_slots[SRNOS_MCP2518FD_MAX_DEVICES];
static uint8_t s_dbg_initialized[SRNOS_MCP2518FD_MAX_DEVICES];

SRNOS_DEBUG_WATCH_BSS volatile SRNOS_DbgMcp2518fd SRNOS_DebugDbgMcp2518fd[SRNOS_MCP2518FD_MAX_DEVICES];

static void spi_adapter_debug_init(CANFDSPI_MODULE_ID index)
{
    if ((index >= SRNOS_MCP2518FD_MAX_DEVICES) || (s_dbg_initialized[index] != 0u))
    {
        return;
    }

    SRNOS_DbgZero(&SRNOS_DebugDbgMcp2518fd[index], sizeof(SRNOS_DebugDbgMcp2518fd[index]));
    SRNOS_DbgInit(&SRNOS_DebugDbgMcp2518fd[index].h,
                 SRNOS_DBG_MODULE_MCP2518FD,
                 (uint16_t)index,
                 (uint16_t)sizeof(SRNOS_DebugDbgMcp2518fd[index]));
    s_dbg_initialized[index] = 1u;
}

static void spi_adapter_yield(void)
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

int8_t SRNOS_MCP2518FDAttachSPI(CANFDSPI_MODULE_ID index,
                            SRNOS_SPIBusId bus_id,
                            GPIO_TypeDef *cs_port,
                            uint16_t cs_pin)
{
    if (index >= SRNOS_MCP2518FD_MAX_DEVICES || !cs_port)
        return -1;

    spi_adapter_debug_init(index);

    spi_adapter_slot_t *slot = &s_slots[index];
    slot->bus_id = bus_id;
    slot->cs_port = cs_port;
    slot->cs_pin = cs_pin;
    slot->configured = 1u;
    slot->ready = 0u;

    return 0;
}

static SRNOS_SPIStatus spi_adapter_prepare(CANFDSPI_MODULE_ID index)
{
    if (index >= SRNOS_MCP2518FD_MAX_DEVICES)
        return SRNOS_SPI_INVALID_PARAM;

    spi_adapter_slot_t *slot = &s_slots[index];

    if (!slot->configured)
    {
        const SRNOS_ExternalCANDevCfg *binding = SRNOS_BoardExternalCANConfig((uint8_t)index);
        if (binding)
        {
            slot->bus_id = binding->bus_id;
            slot->cs_port = binding->cs_port;
            slot->cs_pin = binding->cs_pin;
            slot->configured = 1u;
        }
        else
        {
            return SRNOS_SPI_INVALID_PARAM;
        }
    }

    if (slot->ready)
        return SRNOS_SPI_OK;

    SRNOS_SPIStatus st = SRNOS_SPIDeviceInit(&slot->dev,
                                              slot->bus_id,
                                              slot->cs_port,
                                              slot->cs_pin);
    if (st == SRNOS_SPI_OK)
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
    volatile SRNOS_DbgMcp2518fd *dbg = NULL;
    if (index < SRNOS_MCP2518FD_MAX_DEVICES)
    {
        spi_adapter_debug_init(index);
        dbg = &SRNOS_DebugDbgMcp2518fd[index];
    }

    if (dbg)
    {
        dbg->xfer_calls++;
        dbg->last_index = index;
        dbg->last_size = spiTransferSize;
        SRNOS_DbgEnd(&dbg->h, HAL_GetTick(), SRNOS_SPI_BUSY);
    }

    if (!spiTxData || !spiRxData || (spiTransferSize == 0u))
    {
        if (dbg)
        {
            dbg->xfer_error++;
            dbg->last_ret = (uint32_t)-1;
            SRNOS_DbgEnd(&dbg->h, HAL_GetTick(), SRNOS_SPI_INVALID_PARAM);
        }
        return -1;
    }

    if (spi_adapter_prepare(index) != SRNOS_SPI_OK)
    {
        if (dbg)
        {
            dbg->xfer_error++;
            dbg->last_ret = (uint32_t)-1;
            SRNOS_DbgEnd(&dbg->h, HAL_GetTick(), SRNOS_SPI_ERROR);
        }
        return -1;
    }

    spi_adapter_slot_t *slot = &s_slots[index];
    const uint32_t start = HAL_GetTick();

    for (;;)
    {
        SRNOS_SPIStatus st = SRNOS_SPITransferBlocking(&slot->dev,
                                                        spiTxData,
                                                        spiRxData,
                                                        (size_t)spiTransferSize,
                                                        SRNOS_MCP2518FD_SPI_TIMEOUT_MS);
        if (st == SRNOS_SPI_OK)
        {
            if (dbg)
            {
                dbg->xfer_ok++;
                dbg->last_ret = 0u;
                SRNOS_DbgEnd(&dbg->h, HAL_GetTick(), SRNOS_SPI_OK);
            }
            return 0;
        }
        if (st != SRNOS_SPI_BUSY)
        {
            if (dbg)
            {
                dbg->xfer_error++;
                dbg->last_ret = (uint32_t)-1;
                SRNOS_DbgEnd(&dbg->h, HAL_GetTick(), (uint32_t)st);
            }
            return -1;
        }
        if ((HAL_GetTick() - start) > SRNOS_MCP2518FD_SPI_BUSY_TIMEOUT_MS)
        {
            if (dbg)
            {
                dbg->xfer_timeout++;
                dbg->last_ret = (uint32_t)-2;
                SRNOS_DbgEnd(&dbg->h, HAL_GetTick(), SRNOS_SPI_BUSY);
            }
            return -2;
        }
        if (dbg) dbg->xfer_busy++;
        spi_adapter_yield();
    }
}

void SRNOS_MCP2518FDGetSPIStatistics(CANFDSPI_MODULE_ID index, SRNOS_SPIStatistics *out) {
    if(index<SRNOS_MCP2518FD_MAX_DEVICES) SRNOS_SPIGetStatistics(&s_slots[index].dev,out);
}
