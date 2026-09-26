//
// tskptt_exfdcan_device.cpp — MCP2518FD device config + EXTI dispatch
//
#include "EXFDCAN_Router_Internal.h"
#include "FreeRTOS.h"
#include "SirinEmbedOS/Devices/MCP2518FD/drv_canfdspi_api.h"
#include "SirinEmbedOS/Devices/MCP2518FD/drv_canfdspi_defines.h"
#include "SirinEmbedOS/Devices/MCP2518FD/drv_spi.h"
#include "fixed_resources.h"
#include "stm32h7xx_hal.h"

#ifndef EXFDCAN_SYSCLK
#define EXFDCAN_SYSCLK    CAN_SYSCLK_40M
#endif
#ifndef EXFDCAN_BITTIME
#define EXFDCAN_BITTIME   CAN_1000K_4M
#endif

extern "C" {
OMX_DEBUG_WATCH_BSS volatile exfdcan_stats_t g_omx_dbg_exfdcan[EXFDCAN_DEV_COUNT];
}

static exfdcan_ctx_t s_ctx[EXFDCAN_DEV_COUNT];
static osMutexId_t   s_canfdspi_mutex = NULL;
static uint8_t       s_dbg_initialized[EXFDCAN_DEV_COUNT];

static void exfdcan_dbg_init(uint8_t dev)
{
    if ((dev >= EXFDCAN_DEV_COUNT) || (s_dbg_initialized[dev] != 0u))
    {
        return;
    }

    omx_dbg_zero(&g_omx_dbg_exfdcan[dev], sizeof(g_omx_dbg_exfdcan[dev]));
    omx_dbg_init(&g_omx_dbg_exfdcan[dev].h,
                 OMX_DBG_MODULE_EXFDCAN,
                 dev,
                 (uint16_t)sizeof(g_omx_dbg_exfdcan[dev]));
    s_dbg_initialized[dev] = 1u;
}

static void exfdcan_dbg_touch(exfdcan_ctx_t *ctx, uint32_t status)
{
    if ((ctx == NULL) || (ctx->stats == NULL))
    {
        return;
    }

    omx_dbg_end(&ctx->stats->h, HAL_GetTick(), status);
}

extern "C" exfdcan_ctx_t* exfdcan_ctx_of(uint8_t dev)
{
    if (dev >= EXFDCAN_DEV_COUNT)
        return NULL;

    exfdcan_ctx_t *ctx = &s_ctx[dev];
    exfdcan_dbg_init(dev);
    if (!ctx->stats)
    {
        ctx->stats = &g_omx_dbg_exfdcan[dev];
    }
    return ctx;
}

extern "C" const exfdcan_dev_cfg_t* exfdcan_cfg_of(uint8_t dev)
{
    return SRNOS_BoardExternalCANConfig(dev);
}

extern "C" can_channel_t exfdcan_channel_from_dev(uint8_t dev)
{
    if (dev >= EXFDCAN_DEV_COUNT)
        return 0u;
    return CAN_CH_EXFDCAN(dev);
}

extern "C" int exfdcan_dev_from_channel(can_channel_t ch)
{
    if ((ch < CAN_ROUTER_EXFDCAN_CHANNEL_BASE) ||
        (ch >= (CAN_ROUTER_EXFDCAN_CHANNEL_BASE + EXFDCAN_DEV_COUNT)))
    {
        return -1;
    }
    return (int)(ch - CAN_ROUTER_EXFDCAN_CHANNEL_BASE);
}

extern "C" void exfdcan_bus_lock_init(void)
{
    if (!s_canfdspi_mutex)
    {
        if (!s_canfdspi_mutex)
        {
            s_canfdspi_mutex = osMutexNew(NULL);
        }
    }
}

extern "C" void exfdcan_bus_lock(void)
{
    if (s_canfdspi_mutex)
    {
        (void)osMutexAcquire(s_canfdspi_mutex, osWaitForever);
    }
}

extern "C" void exfdcan_bus_unlock(void)
{
    if (s_canfdspi_mutex)
    {
        (void)osMutexRelease(s_canfdspi_mutex);
    }
}

extern "C" int exfdcan_hw_init(uint8_t dev)
{
    exfdcan_ctx_t *ctx = exfdcan_ctx_of(dev);
    const exfdcan_dev_cfg_t *cfg = exfdcan_cfg_of(dev);
    if (!ctx || !cfg)
        return -1;

    exfdcan_bus_lock_init();
    exfdcan_bus_lock();
    if (ctx->init_done)
    {
        exfdcan_bus_unlock();
        return 0;
    }

#define EXFDCAN_INIT_CHECK(expr)                                      \
    do {                                                              \
        if ((expr) != 0) {                                             \
            ctx->stats->init_errors++;                                \
            exfdcan_dbg_touch(ctx, 1u);                                \
            exfdcan_bus_unlock();                                     \
            return -1;                                                \
        }                                                             \
    } while (0)

    EXFDCAN_INIT_CHECK(DRV_SPI_AttachDevice(dev, cfg->bus_id, cfg->cs_port, cfg->cs_pin));

    EXFDCAN_INIT_CHECK(DRV_CANFDSPI_Reset(dev));
    HAL_Delay(2);
    EXFDCAN_INIT_CHECK(DRV_CANFDSPI_OperationModeSelect(dev, CAN_CONFIGURATION_MODE));

    CAN_CONFIG config;
    EXFDCAN_INIT_CHECK(DRV_CANFDSPI_ConfigureObjectReset(&config));
    config.IsoCrcEnable = 1;
    config.BitRateSwitchDisable = 1;
    config.RestrictReTxAttempts = 1;
    config.TXQEnable = 0;
    config.StoreInTEF = 0;
    config.TxBandWidthSharing = CAN_TXBWS_NO_DELAY;
    EXFDCAN_INIT_CHECK(DRV_CANFDSPI_Configure(dev, &config));

    EXFDCAN_INIT_CHECK(DRV_CANFDSPI_BitTimeConfigure(dev, EXFDCAN_BITTIME, CAN_SSP_MODE_OFF, EXFDCAN_SYSCLK));

    CAN_TX_FIFO_CONFIG txf;
    EXFDCAN_INIT_CHECK(DRV_CANFDSPI_TransmitChannelConfigureObjectReset(&txf));
    txf.TxPriority = 1;
    txf.TxAttempts = 2;
    txf.FifoSize = EXFDCAN_TXF_SIZE;
    txf.PayLoadSize = CAN_PLSIZE_8;
    txf.RTREnable = 0;
    EXFDCAN_INIT_CHECK(DRV_CANFDSPI_TransmitChannelConfigure(dev, EXFDCAN_TX_FIFO, &txf));

    CAN_RX_FIFO_CONFIG rxf;
    EXFDCAN_INIT_CHECK(DRV_CANFDSPI_ReceiveChannelConfigureObjectReset(&rxf));
    rxf.FifoSize = EXFDCAN_RXF_SIZE;
    rxf.PayLoadSize = CAN_PLSIZE_8;
    rxf.RxTimeStampEnable = 0;
    EXFDCAN_INIT_CHECK(DRV_CANFDSPI_ReceiveChannelConfigure(dev, EXFDCAN_RX_FIFO, &rxf));

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
    EXFDCAN_INIT_CHECK(DRV_CANFDSPI_FilterObjectConfigure(dev, CAN_FILTER0, &fobj));
    EXFDCAN_INIT_CHECK(DRV_CANFDSPI_FilterMaskConfigure(dev, CAN_FILTER0, &mobj));
    EXFDCAN_INIT_CHECK(DRV_CANFDSPI_FilterToFifoLink(dev, CAN_FILTER0, EXFDCAN_RX_FIFO, true));
    EXFDCAN_INIT_CHECK(DRV_CANFDSPI_FilterEnable(dev, CAN_FILTER0));

    EXFDCAN_INIT_CHECK(DRV_CANFDSPI_GpioModeConfigure(dev, GPIO_MODE_INT, GPIO_MODE_INT));
    EXFDCAN_INIT_CHECK(DRV_CANFDSPI_GpioDirectionConfigure(dev, GPIO_OUTPUT, GPIO_OUTPUT));
    EXFDCAN_INIT_CHECK(DRV_CANFDSPI_GpioInterruptPinsOpenDrainConfigure(dev, GPIO_OPEN_DRAIN));

    EXFDCAN_INIT_CHECK(DRV_CANFDSPI_ModuleEventEnable(dev,
                                                      (CAN_MODULE_EVENT)(CAN_RX_EVENT |
                                                                         CAN_TX_EVENT |
                                                                         CAN_RX_OVERFLOW_EVENT)));
    EXFDCAN_INIT_CHECK(DRV_CANFDSPI_ReceiveChannelEventEnable(dev,
                                                              EXFDCAN_RX_FIFO,
                                                              (CAN_RX_FIFO_EVENT)(CAN_RX_FIFO_NOT_EMPTY_EVENT |
                                                                                  CAN_RX_FIFO_HALF_FULL_EVENT |
                                                                                  CAN_RX_FIFO_OVERFLOW_EVENT)));
    EXFDCAN_INIT_CHECK(DRV_CANFDSPI_TransmitChannelEventEnable(dev,
                                                               EXFDCAN_TX_FIFO,
                                                               (CAN_TX_FIFO_EVENT)(CAN_TX_FIFO_NOT_FULL_EVENT |
                                                                                   CAN_TX_FIFO_ATTEMPTS_EXHAUSTED_EVENT)));

    EXFDCAN_INIT_CHECK(DRV_CANFDSPI_OperationModeSelect(dev, CAN_NORMAL_MODE));

    ctx->init_done = 1u;
    exfdcan_dbg_touch(ctx, 0u);
    exfdcan_bus_unlock();
#undef EXFDCAN_INIT_CHECK
    return 0;
}

extern "C" void exfdcan_on_exti(uint16_t gpio_pin)
{
    for (uint8_t i = 0; i < EXFDCAN_DEV_COUNT; ++i)
    {
        exfdcan_ctx_t *ctx = exfdcan_ctx_of(i);
        const exfdcan_dev_cfg_t *cfg = exfdcan_cfg_of(i);
        if (!ctx || !cfg)
            continue;
        uint32_t set_flags = 0;

        if (gpio_pin == cfg->int1_pin)
        {
            ctx->stats->isr_int1++;
            set_flags |= EXFDCAN_EVT_RX;
        }
        else if (gpio_pin == cfg->int0_pin)
        {
            ctx->stats->isr_int0++;
            set_flags |= EXFDCAN_EVT_TX;
        }
        else if (gpio_pin == cfg->int_pin)
        {
            ctx->stats->int_irq++;
            ctx->stats->int_master_wake++;
            set_flags |= EXFDCAN_EVT_INT;
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
        exfdcan_dbg_touch(ctx, set_flags);
    }
}

extern "C" const volatile exfdcan_stats_t* exfdcan_get_stats(uint8_t dev)
{
    exfdcan_ctx_t *ctx = exfdcan_ctx_of(dev);
    return ctx ? ctx->stats : NULL;
}

extern "C" size_t exfdcan_rx_read(uint8_t dev, exfdcan_rx_frame_t *out, size_t max)
{
    exfdcan_ctx_t *ctx = exfdcan_ctx_of(dev);
    if (!ctx || !out || max == 0u)
        return 0u;

    size_t n = 0;
    uint32_t ps = __get_PRIMASK(); __disable_irq();
    while ((ctx->rx_tail != ctx->rx_head) && (n < max))
    {
        out[n] = ctx->rx_log[ctx->rx_tail];
        ctx->rx_tail = (uint16_t)((ctx->rx_tail + 1u) % EXFDCAN_RX_LOG_DEPTH);
        n++;
    }
    if (!ps) __enable_irq();

    return n;
}

/* Single writer before scheduler start: neither RX nor TX races allocation. */
extern "C" void SRNOS_CAN_PrepareResources(void)
{
    exfdcan_bus_lock_init();
    configASSERT(s_canfdspi_mutex != nullptr);
    for (uint8_t dev=0; dev<EXFDCAN_DEV_COUNT; ++dev) {
        exfdcan_ctx_t *ctx=exfdcan_ctx_of(dev);
        configASSERT(ctx != nullptr && ctx->evt == nullptr);
        ctx->evt=osEventFlagsNew(nullptr);
        configASSERT(ctx->evt != nullptr);
    }
}
