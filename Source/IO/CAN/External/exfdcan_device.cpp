//
// exfdcan_device.cpp — MCP2518FD device config + EXTI dispatch
//
#include "EXFDCAN_Router_Internal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <cstring>
#include "FreeRTOS.h"
#include "Devices/MCP2518FD/vendor/drv_canfdspi_api.h"
#include "Devices/MCP2518FD/vendor/drv_canfdspi_defines.h"
#include "Devices/MCP2518FD/spi_adapter_internal.h"
#include "IO/can_board.h"
#include "stm32h7xx_hal.h"

#ifndef SRNOS_EXTERNAL_CAN_SYSCLK
#define SRNOS_EXTERNAL_CAN_SYSCLK    CAN_SYSCLK_40M
#endif
#ifndef SRNOS_EXTERNAL_CAN_BITTIME
#define SRNOS_EXTERNAL_CAN_BITTIME   CAN_1000K_4M
#endif

extern "C" {
SRNOS_DEBUG_WATCH_BSS volatile SRNOS_ExternalCANStats SRNOS_DebugDbgExternalCAN[SRNOS_EXTERNAL_CAN_DEV_COUNT];
}

static SRNOS_ExternalCANCtx s_ctx[SRNOS_EXTERNAL_CAN_DEV_COUNT];
static osMutexId_t   s_canfdspi_mutex = NULL;
static uint8_t       s_dbg_initialized[SRNOS_EXTERNAL_CAN_DEV_COUNT];

static void SRNOS_ExternalCANDbgInit(uint8_t dev)
{
    if ((dev >= SRNOS_EXTERNAL_CAN_DEV_COUNT) || (s_dbg_initialized[dev] != 0u))
    {
        return;
    }

    SRNOS_DbgZero(&SRNOS_DebugDbgExternalCAN[dev], sizeof(SRNOS_DebugDbgExternalCAN[dev]));
    SRNOS_DbgInit(&SRNOS_DebugDbgExternalCAN[dev].h,
                 SRNOS_DBG_MODULE_EXFDCAN,
                 dev,
                 (uint16_t)sizeof(SRNOS_DebugDbgExternalCAN[dev]));
    s_dbg_initialized[dev] = 1u;
}

static void SRNOS_ExternalCANDbgTouch(SRNOS_ExternalCANCtx *ctx, uint32_t status)
{
    if ((ctx == NULL) || (ctx->stats == NULL))
    {
        return;
    }

    SRNOS_DbgEnd(&ctx->stats->h, HAL_GetTick(), status);
}

extern "C" SRNOS_ExternalCANCtx* SRNOS_ExternalCANCtxOf(uint8_t dev)
{
    if (dev >= SRNOS_EXTERNAL_CAN_DEV_COUNT)
        return NULL;

    SRNOS_ExternalCANCtx *ctx = &s_ctx[dev];
    SRNOS_ExternalCANDbgInit(dev);
    if (!ctx->stats)
    {
        ctx->stats = &SRNOS_DebugDbgExternalCAN[dev];
    }
    return ctx;
}

extern "C" const SRNOS_ExternalCANDevCfg* SRNOS_ExternalCANCfgOf(uint8_t dev)
{
    return SRNOS_BoardExternalCANConfig(dev);
}

extern "C" SRNOS_CANChannel SRNOS_ExternalCANChannelFromDev(uint8_t dev)
{
    if (dev >= SRNOS_EXTERNAL_CAN_DEV_COUNT)
        return 0u;
    return SRNOS_CAN_CH_EXFDCAN(dev);
}

extern "C" int SRNOS_ExternalCANDevFromChannel(SRNOS_CANChannel ch)
{
    if ((ch < SRNOS_CAN_ROUTER_EXFDCAN_CHANNEL_BASE) ||
        (ch >= (SRNOS_CAN_ROUTER_EXFDCAN_CHANNEL_BASE + SRNOS_EXTERNAL_CAN_DEV_COUNT)))
    {
        return -1;
    }
    return (int)(ch - SRNOS_CAN_ROUTER_EXFDCAN_CHANNEL_BASE);
}

extern "C" void SRNOS_ExternalCANBusLockInit(void)
{
    if (!s_canfdspi_mutex)
    {
        if (!s_canfdspi_mutex)
        {
            s_canfdspi_mutex = osMutexNew(NULL);
        }
    }
}

extern "C" void SRNOS_ExternalCANBusLock(void)
{
    if (s_canfdspi_mutex)
    {
        (void)osMutexAcquire(s_canfdspi_mutex, osWaitForever);
    }
}

extern "C" void SRNOS_ExternalCANBusUnlock(void)
{
    if (s_canfdspi_mutex)
    {
        (void)osMutexRelease(s_canfdspi_mutex);
    }
}

extern "C" int SRNOS_ExternalCANHwInit(uint8_t dev)
{
    SRNOS_ExternalCANCtx *ctx = SRNOS_ExternalCANCtxOf(dev);
    const SRNOS_ExternalCANDevCfg *cfg = SRNOS_ExternalCANCfgOf(dev);
    if (!ctx || !cfg)
        return -1;

    SRNOS_ExternalCANBusLockInit();
    SRNOS_ExternalCANBusLock();
    if (ctx->init_done)
    {
        SRNOS_ExternalCANBusUnlock();
        return 0;
    }

#define SRNOS_EXTERNAL_CAN_INIT_CHECK(expr)                                      \
    do {                                                              \
        if ((expr) != 0) {                                             \
            ctx->stats->init_errors++;                                \
            SRNOS_ExternalCANDbgTouch(ctx, 1u);                                \
            SRNOS_ExternalCANBusUnlock();                                     \
            return -1;                                                \
        }                                                             \
    } while (0)

    SRNOS_EXTERNAL_CAN_INIT_CHECK(SRNOS_MCP2518FDAttachSPI(dev, cfg->bus_id, cfg->cs_port, cfg->cs_pin));

    SRNOS_EXTERNAL_CAN_INIT_CHECK(DRV_CANFDSPI_Reset(dev));
    HAL_Delay(2);
    SRNOS_EXTERNAL_CAN_INIT_CHECK(DRV_CANFDSPI_OperationModeSelect(dev, CAN_CONFIGURATION_MODE));

    CAN_CONFIG config;
    SRNOS_EXTERNAL_CAN_INIT_CHECK(DRV_CANFDSPI_ConfigureObjectReset(&config));
    config.IsoCrcEnable = 1;
    config.BitRateSwitchDisable = 1;
    config.RestrictReTxAttempts = 1;
    config.TXQEnable = 0;
    config.StoreInTEF = 0;
    config.TxBandWidthSharing = CAN_TXBWS_NO_DELAY;
    SRNOS_EXTERNAL_CAN_INIT_CHECK(DRV_CANFDSPI_Configure(dev, &config));

    SRNOS_EXTERNAL_CAN_INIT_CHECK(DRV_CANFDSPI_BitTimeConfigure(dev, SRNOS_EXTERNAL_CAN_BITTIME, CAN_SSP_MODE_OFF, SRNOS_EXTERNAL_CAN_SYSCLK));

    CAN_TX_FIFO_CONFIG txf;
    SRNOS_EXTERNAL_CAN_INIT_CHECK(DRV_CANFDSPI_TransmitChannelConfigureObjectReset(&txf));
    txf.TxPriority = 1;
    txf.TxAttempts = 2;
    txf.FifoSize = SRNOS_EXTERNAL_CAN_TXF_SIZE;
    txf.PayLoadSize = CAN_PLSIZE_8;
    txf.RTREnable = 0;
    SRNOS_EXTERNAL_CAN_INIT_CHECK(DRV_CANFDSPI_TransmitChannelConfigure(dev, SRNOS_EXTERNAL_CAN_TX_FIFO, &txf));

    CAN_RX_FIFO_CONFIG rxf;
    SRNOS_EXTERNAL_CAN_INIT_CHECK(DRV_CANFDSPI_ReceiveChannelConfigureObjectReset(&rxf));
    rxf.FifoSize = SRNOS_EXTERNAL_CAN_RXF_SIZE;
    rxf.PayLoadSize = CAN_PLSIZE_8;
    rxf.RxTimeStampEnable = 0;
    SRNOS_EXTERNAL_CAN_INIT_CHECK(DRV_CANFDSPI_ReceiveChannelConfigure(dev, SRNOS_EXTERNAL_CAN_RX_FIFO, &rxf));

    CAN_FILTEROBJ_ID fobj = {0};
    CAN_MASKOBJ_ID   mobj = {0};
    fobj.SID = 0;
    fobj.EID = 0;
    fobj.SID11 = 0;
    fobj.EXIDE = 0;
    mobj.MSID = 0;
    mobj.MEID = 0;
    mobj.MSID11 = 0;
    mobj.MIDE = 0;
    SRNOS_EXTERNAL_CAN_INIT_CHECK(DRV_CANFDSPI_FilterObjectConfigure(dev, CAN_FILTER0, &fobj));
    SRNOS_EXTERNAL_CAN_INIT_CHECK(DRV_CANFDSPI_FilterMaskConfigure(dev, CAN_FILTER0, &mobj));
    SRNOS_EXTERNAL_CAN_INIT_CHECK(DRV_CANFDSPI_FilterToFifoLink(dev, CAN_FILTER0, SRNOS_EXTERNAL_CAN_RX_FIFO, true));
    SRNOS_EXTERNAL_CAN_INIT_CHECK(DRV_CANFDSPI_FilterEnable(dev, CAN_FILTER0));

    SRNOS_EXTERNAL_CAN_INIT_CHECK(DRV_CANFDSPI_GpioModeConfigure(dev, GPIO_MODE_INT, GPIO_MODE_INT));
    SRNOS_EXTERNAL_CAN_INIT_CHECK(DRV_CANFDSPI_GpioDirectionConfigure(dev, GPIO_OUTPUT, GPIO_OUTPUT));
    SRNOS_EXTERNAL_CAN_INIT_CHECK(DRV_CANFDSPI_GpioInterruptPinsOpenDrainConfigure(dev, GPIO_OPEN_DRAIN));

    SRNOS_EXTERNAL_CAN_INIT_CHECK(DRV_CANFDSPI_ModuleEventEnable(dev,
                                                      (CAN_MODULE_EVENT)(CAN_RX_EVENT |
                                                                         CAN_TX_EVENT |
                                                                         CAN_RX_OVERFLOW_EVENT)));
    SRNOS_EXTERNAL_CAN_INIT_CHECK(DRV_CANFDSPI_ReceiveChannelEventEnable(dev,
                                                              SRNOS_EXTERNAL_CAN_RX_FIFO,
                                                              (CAN_RX_FIFO_EVENT)(CAN_RX_FIFO_NOT_EMPTY_EVENT |
                                                                                  CAN_RX_FIFO_HALF_FULL_EVENT |
                                                                                  CAN_RX_FIFO_OVERFLOW_EVENT)));
    SRNOS_EXTERNAL_CAN_INIT_CHECK(DRV_CANFDSPI_TransmitChannelEventEnable(dev,
                                                               SRNOS_EXTERNAL_CAN_TX_FIFO,
                                                               (CAN_TX_FIFO_EVENT)(CAN_TX_FIFO_NOT_FULL_EVENT |
                                                                                   CAN_TX_FIFO_ATTEMPTS_EXHAUSTED_EVENT)));

    SRNOS_EXTERNAL_CAN_INIT_CHECK(DRV_CANFDSPI_OperationModeSelect(dev, CAN_NORMAL_MODE));

    ctx->init_done = 1u;
    SRNOS_ExternalCANDbgTouch(ctx, 0u);
    SRNOS_ExternalCANBusUnlock();
#undef SRNOS_EXTERNAL_CAN_INIT_CHECK
    return 0;
}

extern "C" void SRNOS_ExternalCANOnExti(uint16_t gpio_pin)
{
    for (uint8_t i = 0; i < SRNOS_EXTERNAL_CAN_DEV_COUNT; ++i)
    {
        SRNOS_ExternalCANCtx *ctx = SRNOS_ExternalCANCtxOf(i);
        const SRNOS_ExternalCANDevCfg *cfg = SRNOS_ExternalCANCfgOf(i);
        if (!ctx || !cfg)
            continue;
        uint32_t set_flags = 0;

        if (gpio_pin == cfg->int1_pin)
        {
            ctx->stats->isr_int1++;
            set_flags |= SRNOS_EXTERNAL_CAN_EVT_RX;
        }
        else if (gpio_pin == cfg->int0_pin)
        {
            ctx->stats->isr_int0++;
            set_flags |= SRNOS_EXTERNAL_CAN_EVT_TX;
        }
        else if (gpio_pin == cfg->int_pin)
        {
            ctx->stats->int_irq++;
            ctx->stats->int_master_wake++;
            set_flags |= SRNOS_EXTERNAL_CAN_EVT_INT;
        }
        else
        {
            continue;
        }

        if (ctx->evt)
        {
            (void)osEventFlagsSet(ctx->evt, set_flags);
        }
        else
        {
            ctx->pending_isr |= set_flags;
        }
        SRNOS_ExternalCANDbgTouch(ctx, set_flags);
    }
}

extern "C" bool SRNOS_ExternalCANGetStats(uint8_t dev, SRNOS_ExternalCANStats *out)
{
    SRNOS_ExternalCANCtx *ctx=SRNOS_ExternalCANCtxOf(dev);
    if(!ctx||!out||__get_IPSR()!=0||osKernelGetState()!=osKernelRunning)return false;
    taskENTER_CRITICAL();
    std::memcpy(out,(const void *)ctx->stats,sizeof(*out));
    taskEXIT_CRITICAL();return true;
}

extern "C" size_t SRNOS_ExternalCANRxRead(uint8_t dev, SRNOS_ExternalCANRxFrame *out, size_t max)
{
    SRNOS_ExternalCANCtx *ctx = SRNOS_ExternalCANCtxOf(dev);
    if (!ctx || !out || max == 0u)
        return 0u;

    size_t n = 0;
    uint32_t ps = __get_PRIMASK(); __disable_irq();
    while ((ctx->rx_tail != ctx->rx_head) && (n < max))
    {
        out[n] = ctx->rx_log[ctx->rx_tail];
        ctx->rx_tail = (uint16_t)((ctx->rx_tail + 1u) % SRNOS_EXTERNAL_CAN_RX_LOG_DEPTH);
        n++;
    }
    if (!ps) __enable_irq();

    return n;
}

/* Single writer before scheduler start: neither RX nor TX races allocation. */
extern "C" void SRNOS_CAN_PrepareResources(void)
{
    SRNOS_ExternalCANBusLockInit();
    configASSERT(s_canfdspi_mutex != nullptr);
    for (uint8_t dev=0; dev<SRNOS_EXTERNAL_CAN_DEV_COUNT; ++dev) {
        SRNOS_ExternalCANCtx *ctx=SRNOS_ExternalCANCtxOf(dev);
        configASSERT(ctx != nullptr && ctx->evt == nullptr);
        ctx->evt=osEventFlagsNew(nullptr);
        configASSERT(ctx->evt != nullptr);
    }
}

#include "Devices/MCP2518FD/spi_adapter_internal.h"
extern "C" void SRNOS_ExternalCANGetSPIStatistics(SRNOS_SPIStatistics *out) { SRNOS_MCP2518FDGetSPIStatistics(0, out); }
