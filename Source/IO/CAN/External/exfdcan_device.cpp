//
// exfdcan_device.cpp — MCP2518FD device config + EXTI dispatch
//
#include "EXFDCAN_Router_Internal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <cstring>
#include "Devices/MCP2518FD/vendor/drv_canfdspi_api.h"
#include "Devices/MCP2518FD/vendor/drv_canfdspi_defines.h"
#include "Devices/MCP2518FD/spi_adapter_internal.h"
#include "IO/can_board.h"
#include "stm32h7xx_hal.h"

/* This board uses the MCP's 40 MHz CAN clock. Only FIFO1/2 consume message
 * RAM: TXQ/TEF and RX timestamp are disabled in both frozen modes. */
static_assert(SRNOS_EXTERNAL_CAN_RX_FIFO==CAN_FIFO_CH1 && SRNOS_EXTERNAL_CAN_TX_FIFO==CAN_FIFO_CH2,
              "CAN4 message RAM layout requires FIFO1/2");
static_assert(SRNOS_EXTERNAL_CAN_RXF_SIZE==31u && SRNOS_EXTERNAL_CAN_TXF_SIZE==31u,
              "MCP FIFO size field is count minus one");
static_assert((SRNOS_EXTERNAL_CAN_RXF_SIZE+SRNOS_EXTERNAL_CAN_TXF_SIZE+2u)*(8u+8u)<=2048u,
              "Classic FIFOs exceed MCP RAM");
static_assert((SRNOS_EXTERNAL_CAN_FD_RX_COUNT+SRNOS_EXTERNAL_CAN_FD_TX_COUNT)*(8u+64u)==2016u,
              "FD FIFOs must fit MCP 2 KiB RAM with TXQ/TEF/timestamp disabled");

extern "C" {
SRNOS_DEBUG_WATCH_BSS volatile SRNOS_ExternalCANStats SRNOS_DebugDbgExternalCAN[SRNOS_EXTERNAL_CAN_DEV_COUNT];
}

static SRNOS_ExternalCANCtx s_ctx[SRNOS_EXTERNAL_CAN_DEV_COUNT];
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

extern "C" void SRNOS_ExternalCANRegisterWorker(SRNOS_ExternalCANCtx *ctx)
{
    const uint32_t key=__get_PRIMASK(); __disable_irq();
    TaskHandle_t self=xTaskGetCurrentTaskHandle();
    configASSERT(ctx->task==nullptr && self!=nullptr);
    ctx->task=self;
    const uint32_t flags=ctx->pending; ctx->pending=0;
    __set_PRIMASK(key);
    if(flags)(void)xTaskNotify(self,flags,eSetBits);
}
extern "C" void SRNOS_ExternalCANNotify(SRNOS_ExternalCANCtx *ctx,uint32_t events)
{
    if(!ctx)return;
    const uint32_t key=__get_PRIMASK(); __disable_irq();
    const TaskHandle_t task=static_cast<TaskHandle_t>(ctx->task);
    if(!task)ctx->pending|=events;
    __set_PRIMASK(key);
    if(!task)return;
    if(__get_IPSR()) {
        BaseType_t woken=pdFALSE;
        (void)xTaskNotifyFromISR(task,events,eSetBits,&woken);
        portYIELD_FROM_ISR(woken);
    } else (void)xTaskNotify(task,events,eSetBits);
}

static int SRNOS_ExternalCANModeSelect(uint8_t dev, CAN_OPERATION_MODE mode)
{
    if(DRV_CANFDSPI_OperationModeSelect(dev,mode)!=0) return -1;
    for(unsigned attempt=0;attempt<SRNOS_EXTERNAL_CAN_MODE_ATTEMPTS;++attempt) {
        const CAN_OPERATION_MODE actual=DRV_CANFDSPI_OperationModeGet(dev);
        if(actual==mode) return 0;
        if(actual==CAN_INVALID_MODE) return -1;
        if(attempt+1u<SRNOS_EXTERNAL_CAN_MODE_ATTEMPTS) (void)osDelay(1u);
    }
    return -1;
}

static bool SRNOS_ExternalCANBitTime(const SRNOS_CANControllerConfig *config,
                                    CAN_BITTIME_SETUP *nominal, CAN_BITTIME_SETUP *data)
{
    if(!config || !SRNOS_CANControllerConfigValid(SRNOS_CAN_CH_EXFDCAN0,config)) return false;
    switch(config->nominal_bitrate) {
        case 125000u: *nominal=CAN_125K_500K;break;
        case 250000u: *nominal=CAN_250K_500K;break;
        case 500000u: *nominal=CAN_500K_1M;break;
        case 1000000u: *nominal=CAN_1000K_4M;break;
        default:return false;
    }
    /* Data timing is unused in Classic/no-BRS transmission. The vendor has
     * no 125/250k data preset: use safe 1M while BRSDIS is set. Nominal/data
     * registers are programmed independently, allowing e.g. 1M/2M. */
    uint32_t data_rate=config->data_bitrate;
    if(!config->brs && data_rate<500000u) data_rate=1000000u;
    switch(data_rate) {
        case 500000u: *data=CAN_250K_500K;break;
        case 1000000u: *data=CAN_500K_1M;break;
        case 2000000u: *data=CAN_500K_2M;break;
        case 4000000u: *data=CAN_500K_4M;break;
        case 5000000u: *data=CAN_500K_5M;break;
        case 8000000u: *data=CAN_500K_8M;break;
        default:return false;
    }
    return true;
}

extern "C" int SRNOS_ExternalCANHwInit(uint8_t dev)
{
    SRNOS_ExternalCANCtx *ctx = SRNOS_ExternalCANCtxOf(dev);
    const SRNOS_ExternalCANDevCfg *cfg = SRNOS_ExternalCANCfgOf(dev);
    SRNOS_CANControllerConfig frozen_controller;
    if(!SRNOS_CANControllerGetConfig(SRNOS_ExternalCANChannelFromDev(dev),&frozen_controller))
        return -1;
    const SRNOS_CANControllerConfig *controller=&frozen_controller;
    CAN_BITTIME_SETUP nominal_time, data_time;
    if (!ctx || !cfg || !SRNOS_ExternalCANBitTime(controller,&nominal_time,&data_time))
        return -1;
    const bool fd=controller->mode==SRNOS_CAN_MODE_FD;

    if (ctx->init_done)
    {
        return 0;
    }

#define SRNOS_EXTERNAL_CAN_INIT_CHECK(expr)                                      \
    do {                                                              \
        if ((expr) != 0) {                                             \
            ctx->stats->init_errors++;                                \
            SRNOS_ExternalCANDbgTouch(ctx, 1u);                                \
            return -1;                                                \
        }                                                             \
    } while (0)

    SRNOS_EXTERNAL_CAN_INIT_CHECK(SRNOS_MCP2518FDAttachSPI(dev, cfg->bus_id, cfg->cs_port, cfg->cs_pin));

    SRNOS_EXTERNAL_CAN_INIT_CHECK(DRV_CANFDSPI_Reset(dev));
    HAL_Delay(2);
    SRNOS_EXTERNAL_CAN_INIT_CHECK(SRNOS_ExternalCANModeSelect(dev, CAN_CONFIGURATION_MODE));

    CAN_CONFIG config;
    SRNOS_EXTERNAL_CAN_INIT_CHECK(DRV_CANFDSPI_ConfigureObjectReset(&config));
    config.IsoCrcEnable = 1;
    config.BitRateSwitchDisable = !controller->brs;
    config.RestrictReTxAttempts = 1;
    config.TXQEnable = 0;
    config.StoreInTEF = 0;
    config.TxBandWidthSharing = CAN_TXBWS_NO_DELAY;
    SRNOS_EXTERNAL_CAN_INIT_CHECK(DRV_CANFDSPI_Configure(dev, &config));

    SRNOS_EXTERNAL_CAN_INIT_CHECK(DRV_CANFDSPI_BitTimeConfigureNominal40MHz(dev, nominal_time));
    /* Vendor selects TDC Auto for >=1M and Off for 500k; its sspMode argument
     * is ignored. BRS remains separately controlled by CiCON and each TX. */
    SRNOS_EXTERNAL_CAN_INIT_CHECK(DRV_CANFDSPI_BitTimeConfigureData40MHz(dev, data_time, CAN_SSP_MODE_AUTO));

    CAN_TX_FIFO_CONFIG txf;
    SRNOS_EXTERNAL_CAN_INIT_CHECK(DRV_CANFDSPI_TransmitChannelConfigureObjectReset(&txf));
    txf.TxPriority = 1;
    txf.TxAttempts = 2;
    txf.FifoSize = fd?SRNOS_EXTERNAL_CAN_FD_TX_COUNT-1u:SRNOS_EXTERNAL_CAN_TXF_SIZE;
    txf.PayLoadSize = fd?CAN_PLSIZE_64:CAN_PLSIZE_8;
    txf.RTREnable = 0;
    SRNOS_EXTERNAL_CAN_INIT_CHECK(DRV_CANFDSPI_TransmitChannelConfigure(dev, SRNOS_EXTERNAL_CAN_TX_FIFO, &txf));

    CAN_RX_FIFO_CONFIG rxf;
    SRNOS_EXTERNAL_CAN_INIT_CHECK(DRV_CANFDSPI_ReceiveChannelConfigureObjectReset(&rxf));
    rxf.FifoSize = fd?SRNOS_EXTERNAL_CAN_FD_RX_COUNT-1u:SRNOS_EXTERNAL_CAN_RXF_SIZE;
    rxf.PayLoadSize = fd?CAN_PLSIZE_64:CAN_PLSIZE_8;
    rxf.RxTimeStampEnable = 0;
    configASSERT(!config.TXQEnable && !config.StoreInTEF && !rxf.RxTimeStampEnable);
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

    SRNOS_EXTERNAL_CAN_INIT_CHECK(SRNOS_ExternalCANModeSelect(dev, fd?CAN_NORMAL_MODE:CAN_CLASSIC_MODE));

    SRNOS_MCPFifoInit(&ctx->rx_fifo,dev,false,fd);
    SRNOS_MCPFifoInit(&ctx->tx_fifo,dev,true,fd);
    ctx->init_done = 1u;
    SRNOS_ExternalCANDbgTouch(ctx, 0u);
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

        SRNOS_ExternalCANNotify(ctx,set_flags);
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

/* Notification storage is in the sole owner TCB; startup IRQs stay pending. */
extern "C" void SRNOS_CAN_PrepareResources(void)
{
    for (uint8_t dev=0; dev<SRNOS_EXTERNAL_CAN_DEV_COUNT; ++dev) {
        SRNOS_ExternalCANCtx *ctx=SRNOS_ExternalCANCtxOf(dev);
        configASSERT(ctx != nullptr && ctx->task == nullptr);
    }
    configASSERT(SRNOS_CANFDCorePrepareResources());
}

#include "Devices/MCP2518FD/spi_adapter_internal.h"
extern "C" void SRNOS_ExternalCANGetSPIStatistics(SRNOS_SPIStatistics *out) { SRNOS_MCP2518FDGetSPIStatistics(0, out); }
