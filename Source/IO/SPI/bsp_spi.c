//
// Created by sirin on 2025/11/29.
//
#include "SirinEmbedOS/IO/SPI/bsp_spi.h"
#include "SirinEmbedOS/Services/Logging/bsp_srn_log.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "SirinEmbedOS/Core/C/Fault.h"
#include "SirinEmbedOS/Core/C/Memory.h"

#include "fixed_buses.h"

/* ============================================================
 * Internal bus structures
 * ============================================================ */
typedef enum
{
    BSP_SPI_XFER_IDLE = 0,
    BSP_SPI_XFER_BUSY,
    BSP_SPI_XFER_COMPLETE
} bsp_spi_xfer_state_t;

typedef enum
{
    BSP_SPI_PENDING_NONE = 0,
    BSP_SPI_PENDING_OK,
    BSP_SPI_PENDING_ERROR
} bsp_spi_pending_t;

#define BSP_SPI_DIAG_SPURIOUS_COMPLETE (1u << 0)
#define BSP_SPI_DIAG_PENDING_OVERWRITE (1u << 1)
#define BSP_SPI_DIAG_CB_OVERFLOW       (1u << 2)
#define BSP_SPI_DIAG_RELEASE_ERROR     (1u << 3)
#define BSP_SPI_DIAG_START_CONFLICT    (1u << 4)

typedef struct bsp_spi_bus
{
    SPI_HandleTypeDef      *hspi;
    DMA_HandleTypeDef      *hdma_tx;
    DMA_HandleTypeDef      *hdma_rx;
    osSemaphoreId_t         sem;
    volatile uint8_t        lock_held;
    osThreadId_t            lock_owner;

    volatile bsp_spi_xfer_state_t state;

    bsp_spi_device_t *current_dev;
    bsp_spi_callback_t current_cb;
    void *current_cb_ctx;
    const uint8_t *user_tx_buf;
    uint8_t *user_rx_buf;
    size_t current_len;
    uint8_t rx_copy_back;
    uint8_t direct_xfer;
    volatile uint8_t launch_guard;
    uint8_t blocking_owner;
    volatile bsp_spi_pending_t completion_pending;
    volatile uint32_t diag_flags;
    volatile bsp_spi_status_t last_status;

} bsp_spi_bus_t;

/* One bus for now */
static bsp_spi_bus_t s_spi_buses[BSP_SPI_BUS_MAX] =
{
    [BSP_SPI_BUS_4] =
    {
        .hspi = NULL,
        .hdma_tx = NULL,
        .hdma_rx = NULL,
        .sem = NULL,
        .lock_held = 0u,
        .lock_owner = NULL,
        .state = BSP_SPI_XFER_IDLE,
        .current_dev = NULL,
        .current_cb = NULL,
        .current_cb_ctx = NULL,
        .user_tx_buf = NULL,
        .user_rx_buf = NULL,
        .current_len = 0u,
        .rx_copy_back = 0u,
        .direct_xfer = 0u,
        .launch_guard = 0u,
        .blocking_owner = 0u,
        .completion_pending = BSP_SPI_PENDING_NONE,
        .diag_flags = 0u,
        .last_status = BSP_SPI_OK
    }
};

OMX_DEBUG_WATCH_BSS volatile omx_dbg_spi_t g_omx_dbg_spi[BSP_SPI_BUS_MAX];

static uint8_t s_spi_tx_stage[BSP_SPI_BUS_MAX][BSP_SPI_DMA_STAGING_SIZE] BSP_SPI_DMA_SEC;
static uint8_t s_spi_rx_stage[BSP_SPI_BUS_MAX][BSP_SPI_DMA_STAGING_SIZE] BSP_SPI_DMA_SEC;

#define BSP_SPI_CB_QUEUE_LEN         8u
#define BSP_SPI_CB_THREAD_FLAG_EVENT 0x00000001u

typedef struct
{
    bsp_spi_device_t   *dev;
    bsp_spi_callback_t  cb;
    void               *ctx;
    bsp_spi_status_t    status;
} bsp_spi_cb_event_t;

static bsp_spi_cb_event_t s_spi_cb_queue[BSP_SPI_CB_QUEUE_LEN];
static volatile uint8_t s_spi_cb_head;
static volatile uint8_t s_spi_cb_tail;
static osThreadId_t s_spi_cb_thread;

static void bsp_spi_cb_thread(void *argument);
static void bsp_spi_bus_release_common(bsp_spi_bus_t *bus);
static void bsp_spi_publish_completion(bsp_spi_bus_t *bus,
                                       bsp_spi_pending_t completion);

static const osThreadAttr_t s_spi_cb_thread_attr =
{
    .name = "bsp_spi_cb",
    .stack_size = 768u * 4u,
    .priority = (osPriority_t)osPriorityHigh6
};

/* ============================================================
 * Local helpers
 * ============================================================ */
static bsp_spi_bus_t *bsp_spi_get_bus_by_id(bsp_spi_bus_id_t id)
{
    if (id >= BSP_SPI_BUS_MAX) return NULL;
    return &s_spi_buses[id];
}

static bsp_spi_bus_t *bsp_spi_get_bus_by_handle(SPI_HandleTypeDef *hspi)
{
    for (int i = 0; i < BSP_SPI_BUS_MAX; i++)
    {
        if (s_spi_buses[i].hspi == hspi)
            return &s_spi_buses[i];
    }
    return NULL;
}

static inline volatile omx_dbg_spi_t *bsp_spi_dbg_of(bsp_spi_bus_t *bus)
{
    if (!bus) return NULL;
    ptrdiff_t idx = bus - s_spi_buses;
    if ((idx < 0) || (idx >= BSP_SPI_BUS_MAX)) return NULL;
    return &g_omx_dbg_spi[idx];
}

static inline int32_t bsp_spi_bus_index(const bsp_spi_bus_t *bus)
{
    if (!bus) return -1;
    ptrdiff_t idx = bus - s_spi_buses;
    if ((idx < 0) || (idx >= BSP_SPI_BUS_MAX)) return -1;
    return (int32_t)idx;
}

static inline uint8_t bsp_spi_can_log(void)
{
    return (uint8_t)((__get_IPSR() == 0U) &&
                     (osKernelGetState() == osKernelRunning));
}

static void bsp_spi_diag_set(bsp_spi_bus_t *bus, uint32_t flags)
{
    if (!bus || flags == 0u) return;
    uint32_t ps = __get_PRIMASK(); __disable_irq();
    bus->diag_flags |= flags;
    if (!ps) __enable_irq();
}

static void bsp_spi_log_pending_diag(bsp_spi_bus_t *bus)
{
    if (!bus || !bsp_spi_can_log()) return;

    uint32_t ps = __get_PRIMASK(); __disable_irq();
    uint32_t flags = bus->diag_flags;
    bus->diag_flags = 0u;
    if (!ps) __enable_irq();

    if (flags == 0u) return;

    int32_t idx = bsp_spi_bus_index(bus);
    volatile omx_dbg_spi_t *dbg = bsp_spi_dbg_of(bus);
    if ((flags & BSP_SPI_DIAG_SPURIOUS_COMPLETE) != 0u)
    {
        LOGE_SRC("BSP_SPI", "bus=%ld spurious completion count=%lu state=%lu",
                 (long)idx,
                 (unsigned long)(dbg ? dbg->spurious_complete : 0u),
                 (unsigned long)bus->state);
    }
    if ((flags & BSP_SPI_DIAG_PENDING_OVERWRITE) != 0u)
    {
        LOGE_SRC("BSP_SPI", "bus=%ld completion pending overwrite count=%lu",
                 (long)idx,
                 (unsigned long)(dbg ? dbg->pending_overwrite : 0u));
    }
    if ((flags & BSP_SPI_DIAG_CB_OVERFLOW) != 0u)
    {
        LOGE_SRC("BSP_SPI", "callback queue overflow count=%lu",
                 (unsigned long)(dbg ? dbg->cb_queue_overflow : 0u));
    }
    if ((flags & BSP_SPI_DIAG_RELEASE_ERROR) != 0u)
    {
        LOGE_SRC("BSP_SPI", "bus=%ld semaphore release error count=%lu status=%lu",
                 (long)idx,
                 (unsigned long)(dbg ? dbg->lock_release_error : 0u),
                 (unsigned long)(dbg ? dbg->lock_release_status : 0u));
    }
    if ((flags & BSP_SPI_DIAG_START_CONFLICT) != 0u)
    {
        LOGE_SRC("BSP_SPI", "bus=%ld HAL launch/completion state conflict count=%lu",
                 (long)idx,
                 (unsigned long)(dbg ? dbg->start_conflict : 0u));
    }
}

static uint32_t bsp_spi_cb_queue_depth(void)
{
    uint8_t head = s_spi_cb_head;
    uint8_t tail = s_spi_cb_tail;
    if (tail >= head)
    {
        return (uint32_t)(tail - head);
    }
    return (uint32_t)(BSP_SPI_CB_QUEUE_LEN - head + tail);
}

static uint8_t bsp_spi_cb_enqueue(bsp_spi_device_t *dev,
                                  bsp_spi_callback_t cb,
                                  void *ctx,
                                  bsp_spi_status_t status)
{
    if (!dev || !cb)
    {
        return 1u;
    }

    volatile omx_dbg_spi_t *dbg = bsp_spi_dbg_of(dev->bus);
    uint8_t ok = 0u;
    uint32_t ps = __get_PRIMASK(); __disable_irq();
    uint8_t next_tail = (uint8_t)((s_spi_cb_tail + 1u) % BSP_SPI_CB_QUEUE_LEN);
    if (next_tail != s_spi_cb_head)
    {
        s_spi_cb_queue[s_spi_cb_tail] = (bsp_spi_cb_event_t)
        {
            .dev = dev,
            .cb = cb,
            .ctx = ctx,
            .status = status
        };
        s_spi_cb_tail = next_tail;
        ok = 1u;
        if (dbg)
        {
            dbg->cb_queued++;
        }
    }
    else if (dbg)
    {
        dbg->cb_queue_overflow++;
        bsp_spi_diag_set(dev->bus, BSP_SPI_DIAG_CB_OVERFLOW);
    }
    if (!ps) __enable_irq();

    if ((ok != 0u) && s_spi_cb_thread)
    {
        osThreadFlagsSet(s_spi_cb_thread, BSP_SPI_CB_THREAD_FLAG_EVENT);
        if (dbg)
        {
            dbg->cb_thread_wake++;
        }
    }

    return ok;
}

static uint8_t bsp_spi_cb_dequeue(bsp_spi_cb_event_t *event)
{
    if (!event)
    {
        return 0u;
    }

    uint8_t ok = 0u;
    uint32_t ps = __get_PRIMASK(); __disable_irq();
    if (s_spi_cb_head != s_spi_cb_tail)
    {
        *event = s_spi_cb_queue[s_spi_cb_head];
        s_spi_cb_head = (uint8_t)((s_spi_cb_head + 1u) % BSP_SPI_CB_QUEUE_LEN);
        ok = 1u;
    }
    if (!ps) __enable_irq();
    return ok;
}

static void bsp_spi_cb_thread(void *argument)
{
    (void)argument;
    for (;;)
    {
        (void)osThreadFlagsWait(BSP_SPI_CB_THREAD_FLAG_EVENT,
                                osFlagsWaitAny,
                                osWaitForever);
        bsp_spi_cb_event_t event;
        while (bsp_spi_cb_dequeue(&event) != 0u)
        {
            if (event.cb && event.dev)
            {
                event.cb(event.dev, event.ctx, event.status);
            }
        }
    }
}

static void bsp_spi_clear_xfer(bsp_spi_bus_t *bus)
{
    if (!bus) return;
    bus->state = BSP_SPI_XFER_IDLE;
    bus->current_dev = NULL;
    bus->current_cb = NULL;
    bus->current_cb_ctx = NULL;
    bus->user_tx_buf = NULL;
    bus->user_rx_buf = NULL;
    bus->current_len = 0u;
    bus->rx_copy_back = 0u;
    bus->direct_xfer = 0u;
    bus->launch_guard = 0u;
    bus->blocking_owner = 0u;
    bus->completion_pending = BSP_SPI_PENDING_NONE;
}

static void bsp_spi_copy_rx_if_needed(bsp_spi_bus_t *bus)
{
    int32_t idx = bsp_spi_bus_index(bus);
    if ((idx < 0) || !bus->rx_copy_back || !bus->user_rx_buf)
    {
        return;
    }
    if (bus->current_len > BSP_SPI_DMA_STAGING_SIZE)
    {
        return;
    }

    SRNOS_ASSERT(SRNOS_Memory_Copy(
        SRNOS_Memory_MutableBytes(bus->user_rx_buf, bus->current_len),
        SRNOS_Memory_Bytes(s_spi_rx_stage[idx], bus->current_len)));
}

static inline void bsp_spi_cs_low(bsp_spi_device_t *dev)
{
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_RESET);
}

static inline void bsp_spi_cs_high(bsp_spi_device_t *dev)
{
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_SET);
}

static inline void bsp_spi_wait_yield(uint32_t now_tick, uint32_t *last_yield_tick)
{
    if (__get_IPSR() != 0U)
    {
        __NOP();
        return;
    }

    if (osKernelGetState() != osKernelRunning)
    {
        __NOP();
        return;
    }

    if (now_tick != *last_yield_tick)
    {
        *last_yield_tick = now_tick;
        osDelay(1);
    }
    else
    {
        osDelay(0);
    }
}

static uint32_t bsp_spi_ms_to_ticks(uint32_t ms)
{
    if (ms == 0u) return 0u;
    uint32_t hz = osKernelGetTickFreq();
    uint64_t num = (uint64_t)ms * (uint64_t)hz + 999ull;
    uint32_t t = (uint32_t)(num / 1000ull);
    return (t == 0u) ? 1u : t;
}

/* ============================================================
 * PUBLIC API
 * ============================================================ */
void bsp_spi_init_pre(void)
{
    s_spi_cb_head = 0u;
    s_spi_cb_tail = 0u;
    for (int i = 0; i < BSP_SPI_BUS_MAX; i++)
    {
        SRNOS_BoardSPIHandles((uint8_t)i, &s_spi_buses[i].hspi,
                             &s_spi_buses[i].hdma_tx, &s_spi_buses[i].hdma_rx);
        s_spi_buses[i].state = BSP_SPI_XFER_IDLE;
        s_spi_buses[i].current_dev = NULL;
        s_spi_buses[i].current_cb = NULL;
        s_spi_buses[i].current_cb_ctx = NULL;
        s_spi_buses[i].user_tx_buf = NULL;
        s_spi_buses[i].user_rx_buf = NULL;
        s_spi_buses[i].current_len = 0u;
        s_spi_buses[i].rx_copy_back = 0u;
        s_spi_buses[i].direct_xfer = 0u;
        s_spi_buses[i].launch_guard = 0u;
        s_spi_buses[i].blocking_owner = 0u;
        s_spi_buses[i].completion_pending = BSP_SPI_PENDING_NONE;
        s_spi_buses[i].diag_flags = 0u;
        s_spi_buses[i].last_status = BSP_SPI_OK;
        s_spi_buses[i].lock_held = 0u;
        s_spi_buses[i].lock_owner = NULL;
        omx_dbg_zero(&g_omx_dbg_spi[i], sizeof(g_omx_dbg_spi[i]));
        omx_dbg_init(&g_omx_dbg_spi[i].h,
                     OMX_DBG_MODULE_SPI,
                     (uint16_t)i,
                     (uint16_t)sizeof(g_omx_dbg_spi[i]));
        g_omx_dbg_spi[i].state = BSP_SPI_XFER_IDLE;
        g_omx_dbg_spi[i].h.last_status = BSP_SPI_OK;
    }
}

void bsp_spi_init_rtos(void)
{
    if (!s_spi_cb_thread)
    {
        s_spi_cb_thread = osThreadNew(bsp_spi_cb_thread, NULL, &s_spi_cb_thread_attr);
    }

    for (int i = 0; i < BSP_SPI_BUS_MAX; i++)
    {
        if (!s_spi_buses[i].sem)
        {
            s_spi_buses[i].sem = osSemaphoreNew(1u, 1u, NULL);
        }
        configASSERT(s_spi_cb_thread != NULL && s_spi_buses[i].sem != NULL);
        s_spi_buses[i].lock_held = 0u;
        s_spi_buses[i].lock_owner = NULL;
    }
}

bsp_spi_status_t bsp_spi_device_init(bsp_spi_device_t *dev,
                                     bsp_spi_bus_id_t bus_id,
                                     GPIO_TypeDef *cs_port,
                                     uint16_t cs_pin)
{
    if (!dev || !cs_port) return BSP_SPI_INVALID_PARAM;

    bsp_spi_bus_t *bus = bsp_spi_get_bus_by_id(bus_id);
    if (!bus || !bus->hspi) return BSP_SPI_ERROR;

    dev->bus = bus;
    dev->cs_port = cs_port;
    dev->cs_pin = cs_pin;

    dev->max_speed_hz = 0;
    dev->mode = 0;
    dev->bits_per_word = 8;

    bsp_spi_cs_high(dev);
    return BSP_SPI_OK;
}

/* ============================================================
 * Async DMA transfer
 * ============================================================ */
static bsp_spi_status_t bsp_spi_start_dma(bsp_spi_device_t *dev,
                                          const uint8_t *tx_buf,
                                          uint8_t *rx_buf,
                                          size_t len,
                                          bsp_spi_callback_t cb,
                                          void *user_ctx,
                                          uint8_t direct,
                                          uint8_t blocking_owner)
{
    if (!dev || !dev->bus || len == 0u || len > 0xFFFFu || (!tx_buf && !rx_buf))
    {
        if (bsp_spi_can_log())
        {
            LOGE_SRC("BSP_SPI", "invalid transfer dev=%p tx=%p rx=%p len=%lu",
                     (void *)dev, (const void *)tx_buf, (void *)rx_buf,
                     (unsigned long)len);
        }
        return BSP_SPI_INVALID_PARAM;
    }

    bsp_spi_bus_t *bus = dev->bus;
    int32_t bus_idx = bsp_spi_bus_index(bus);
    if (bus_idx < 0)
    {
        return BSP_SPI_ERROR;
    }
    if ((!direct) && (len > BSP_SPI_DMA_STAGING_SIZE))
    {
        return BSP_SPI_INVALID_PARAM;
    }

    volatile omx_dbg_spi_t *dbg = bsp_spi_dbg_of(bus);
    bsp_spi_log_pending_diag(bus);

    if (dbg)
    {
        dbg->last_len = (uint32_t)len;
        dbg->h.last_tick = HAL_GetTick();
        dbg->state = bus->state;
        dbg->hal_state = bus->hspi ? (uint32_t)HAL_SPI_GetState(bus->hspi) : 0u;
        dbg->hal_error = bus->hspi ? (uint32_t)HAL_SPI_GetError(bus->hspi) : 0u;
        dbg->last_dev = (uintptr_t)dev;
        dbg->last_cs_port = (uintptr_t)dev->cs_port;
        dbg->last_cs_pin = dev->cs_pin;
    }

    uint32_t ps = __get_PRIMASK(); __disable_irq();
    if (bus->state != BSP_SPI_XFER_IDLE)
    {
        if (!ps) __enable_irq();
        if (dbg) dbg->xfer_busy++;
        return BSP_SPI_BUSY;
    }

    bus->state = BSP_SPI_XFER_BUSY;
    bus->current_dev = dev;
    bus->current_cb = cb;
    bus->current_cb_ctx = user_ctx;
    bus->user_tx_buf = tx_buf;
    bus->user_rx_buf = rx_buf;
    bus->current_len = len;
    bus->rx_copy_back = (uint8_t)((!direct) && (rx_buf != NULL));
    bus->direct_xfer = direct;
    bus->launch_guard = 1u;
    bus->blocking_owner = (uint8_t)(blocking_owner != 0u);
    bus->completion_pending = BSP_SPI_PENDING_NONE;
    bus->last_status = BSP_SPI_BUSY;
    if (dbg)
    {
        dbg->state = BSP_SPI_XFER_BUSY;
        dbg->launch_guard = 1u;
        dbg->pending_status = BSP_SPI_PENDING_NONE;
    }
    if (!ps) __enable_irq();

    uint8_t *hal_tx = (uint8_t *)tx_buf;
    uint8_t *hal_rx = rx_buf;
    if (!direct)
    {
        if (tx_buf)
        {
            SRNOS_ASSERT(SRNOS_Memory_Copy(
                SRNOS_Memory_MutableBytes(s_spi_tx_stage[bus_idx], sizeof(s_spi_tx_stage[bus_idx])),
                SRNOS_Memory_Bytes(tx_buf, len)));
            hal_tx = s_spi_tx_stage[bus_idx];
        }
        if (rx_buf)
        {
            hal_rx = s_spi_rx_stage[bus_idx];
        }
    }

    bsp_spi_cs_low(dev);

    HAL_StatusTypeDef hal_ret;

    if (tx_buf && rx_buf)
        hal_ret = HAL_SPI_TransmitReceive_DMA(bus->hspi, hal_tx, hal_rx, len);
    else if (tx_buf)
        hal_ret = HAL_SPI_Transmit_DMA(bus->hspi, hal_tx, len);
    else
        hal_ret = HAL_SPI_Receive_DMA(bus->hspi, hal_rx, len);

    if (dbg)
    {
        dbg->last_hal_ret = (uint32_t)hal_ret;
        if (hal_ret == HAL_OK)
        {
            dbg->xfer_start++;
        }
    }

    ps = __get_PRIMASK(); __disable_irq();
    bsp_spi_pending_t pending = bus->completion_pending;
    bus->completion_pending = BSP_SPI_PENDING_NONE;
    bus->launch_guard = 0u;
    if (dbg)
    {
        dbg->launch_guard = 0u;
        dbg->pending_status = BSP_SPI_PENDING_NONE;
    }
    if (!ps) __enable_irq();

    if (hal_ret != HAL_OK)
    {
        if (dbg)
        {
            dbg->xfer_error++;
            dbg->hal_state = bus->hspi ? (uint32_t)HAL_SPI_GetState(bus->hspi) : 0u;
            dbg->hal_error = bus->hspi ? (uint32_t)HAL_SPI_GetError(bus->hspi) : 0u;
            if (pending != BSP_SPI_PENDING_NONE)
            {
                dbg->start_conflict++;
            }
        }
        if (pending != BSP_SPI_PENDING_NONE)
        {
            bsp_spi_diag_set(bus, BSP_SPI_DIAG_START_CONFLICT);
        }
        if (bsp_spi_can_log())
        {
            LOGE_SRC("BSP_SPI",
                     "bus=%ld HAL DMA start failed ret=%lu state=%lu error=0x%08lx lock=%lu rx_dma=%lu tx_dma=%lu pending=%lu dev=%p len=%lu",
                     (long)bus_idx,
                     (unsigned long)hal_ret,
                     (unsigned long)(bus->hspi ? bus->hspi->State : 0u),
                     (unsigned long)(bus->hspi ? bus->hspi->ErrorCode : 0u),
                     (unsigned long)(bus->hspi ? bus->hspi->Lock : 0u),
                     (unsigned long)(bus->hdma_rx ? bus->hdma_rx->State : 0u),
                     (unsigned long)(bus->hdma_tx ? bus->hdma_tx->State : 0u),
                     (unsigned long)pending,
                     (void *)dev,
                     (unsigned long)len);
        }
        bus->last_status = BSP_SPI_ERROR;
        bsp_spi_clear_xfer(bus);
        bsp_spi_cs_high(dev);
        return BSP_SPI_ERROR;
    }

    if (pending != BSP_SPI_PENDING_NONE)
    {
        if (dbg)
        {
            dbg->deferred_publish++;
            if (dbg->deferred_publish == 1u && bsp_spi_can_log())
            {
                LOGD_SRC("BSP_SPI",
                         "bus=%ld early completion deferred until HAL launch returned",
                         (long)bus_idx);
            }
        }
        bsp_spi_publish_completion(bus, pending);
    }

    return BSP_SPI_OK;
}

bsp_spi_status_t bsp_spi_transfer_dma(bsp_spi_device_t *dev,
                                      const uint8_t *tx_buf,
                                      uint8_t *rx_buf,
                                      size_t len,
                                      bsp_spi_callback_t cb,
                                      void *user_ctx)
{
    bsp_spi_status_t lock_st = bsp_spi_bus_acquire(dev, 0u);
    if (lock_st != BSP_SPI_OK) return lock_st;

    bsp_spi_status_t ret = bsp_spi_start_dma(dev, tx_buf, rx_buf, len,
                                             cb, user_ctx, 0u, 0u);
    if (ret != BSP_SPI_OK) bsp_spi_bus_release_isr(dev);
    return ret;
}

bsp_spi_status_t bsp_spi_transfer_dma_direct(bsp_spi_device_t *dev,
                                             const uint8_t *tx_buf,
                                             uint8_t *rx_buf,
                                             size_t len,
                                             bsp_spi_callback_t cb,
                                             void *user_ctx)
{
    bsp_spi_status_t lock_st = bsp_spi_bus_acquire(dev, 0u);
    if (lock_st != BSP_SPI_OK) return lock_st;

    bsp_spi_status_t ret = bsp_spi_start_dma(dev, tx_buf, rx_buf, len,
                                             cb, user_ctx, 1u, 0u);
    if (ret != BSP_SPI_OK) bsp_spi_bus_release_isr(dev);
    return ret;
}

static void bsp_spi_bus_release_common(bsp_spi_bus_t *bus)
{
    if (!bus) return;

    volatile omx_dbg_spi_t *dbg = bsp_spi_dbg_of(bus);
    if (!bus->lock_held)
    {
        return;
    }

    bus->lock_held = 0u;
    bus->lock_owner = NULL;
    if (dbg)
    {
        dbg->h.last_tick = HAL_GetTick();
        dbg->h.last_status = BSP_SPI_OK;
        dbg->lock_last_fail = BSP_SPI_LOCK_FAIL_NONE;
        dbg->lock_release++;
        dbg->lock_held = bus->lock_held;
        dbg->lock_owner = (uintptr_t)bus->lock_owner;
    }

    if (bus->sem)
    {
        osStatus_t st = osSemaphoreRelease(bus->sem);
        if (dbg) dbg->lock_release_status = (uint32_t)st;
        if ((dbg) && (st != osOK))
        {
            dbg->lock_release_error++;
            bsp_spi_diag_set(bus, BSP_SPI_DIAG_RELEASE_ERROR);
        }
    }
}

static void bsp_spi_publish_completion(bsp_spi_bus_t *bus,
                                       bsp_spi_pending_t completion)
{
    if (!bus || completion == BSP_SPI_PENDING_NONE) return;

    bsp_spi_device_t *dev = bus->current_dev;
    bsp_spi_callback_t cb = bus->current_cb;
    void *ctx = bus->current_cb_ctx;
    bsp_spi_status_t status = (completion == BSP_SPI_PENDING_OK)
        ? BSP_SPI_OK
        : BSP_SPI_ERROR;
    volatile omx_dbg_spi_t *dbg = bsp_spi_dbg_of(bus);

    bus->last_status = status;
    bus->state = BSP_SPI_XFER_COMPLETE;

    if (dbg)
    {
        if (status == BSP_SPI_OK)
        {
            dbg->xfer_done++;
        }
        else
        {
            dbg->xfer_error++;
        }
        dbg->h.last_tick = HAL_GetTick();
        dbg->cb_status = (uint32_t)status;
        dbg->last_dev = (uintptr_t)dev;
        if (dev)
        {
            dbg->last_cs_port = (uintptr_t)dev->cs_port;
            dbg->last_cs_pin = dev->cs_pin;
        }
        dbg->hal_state = bus->hspi ? (uint32_t)HAL_SPI_GetState(bus->hspi) : 0u;
        dbg->hal_error = bus->hspi ? (uint32_t)HAL_SPI_GetError(bus->hspi) : 0u;
        dbg->state = BSP_SPI_XFER_COMPLETE;
    }

    if (bus->blocking_owner != 0u)
    {
        return;
    }

    bsp_spi_clear_xfer(bus);
    if (dbg) dbg->state = BSP_SPI_XFER_IDLE;
    bsp_spi_bus_release_common(bus);
    (void)bsp_spi_cb_enqueue(dev, cb, ctx, status);
}

static bsp_spi_status_t bsp_spi_finish_blocking_completion(bsp_spi_bus_t *bus)
{
    if (!bus) return BSP_SPI_INVALID_PARAM;

    volatile omx_dbg_spi_t *dbg = bsp_spi_dbg_of(bus);
    uint32_t ps = __get_PRIMASK(); __disable_irq();
    if ((bus->state != BSP_SPI_XFER_COMPLETE) || (bus->blocking_owner == 0u))
    {
        if (dbg) dbg->start_conflict++;
        bsp_spi_clear_xfer(bus);
        if (dbg) dbg->state = BSP_SPI_XFER_IDLE;
        if (!ps) __enable_irq();
        bsp_spi_diag_set(bus, BSP_SPI_DIAG_START_CONFLICT);
        bsp_spi_bus_release_common(bus);
        return BSP_SPI_ERROR;
    }

    bsp_spi_device_t *dev = bus->current_dev;
    bsp_spi_callback_t cb = bus->current_cb;
    void *ctx = bus->current_cb_ctx;
    bsp_spi_status_t status = bus->last_status;
    bsp_spi_clear_xfer(bus);
    if (dbg) dbg->state = BSP_SPI_XFER_IDLE;
    if (!ps) __enable_irq();

    bsp_spi_bus_release_common(bus);
    (void)bsp_spi_cb_enqueue(dev, cb, ctx, status);
    return status;
}

bsp_spi_status_t bsp_spi_bus_acquire(bsp_spi_device_t *dev, uint32_t timeout_ms)
{
    if (!dev || !dev->bus) return BSP_SPI_INVALID_PARAM;

    bsp_spi_bus_t *bus = dev->bus;
    volatile omx_dbg_spi_t *dbg = bsp_spi_dbg_of(bus);

    if (dbg)
    {
        dbg->h.last_tick = HAL_GetTick();
        dbg->lock_held = bus->lock_held;
        dbg->lock_owner = (uintptr_t)bus->lock_owner;
        dbg->lock_os_status = 0u;
    }

    if (__get_IPSR() != 0U)
    {
        if (dbg)
        {
            dbg->h.last_status = BSP_SPI_BUSY;
            dbg->lock_last_fail = BSP_SPI_LOCK_FAIL_IN_ISR;
            dbg->lock_busy++;
        }
        return BSP_SPI_BUSY;
    }

    if (osKernelGetState() != osKernelRunning)
    {
        if (dbg)
        {
            dbg->h.last_status = BSP_SPI_OK;
            dbg->lock_last_fail = BSP_SPI_LOCK_FAIL_KERNEL_NOT_RUNNING;
            dbg->lock_ok++;
        }
        return BSP_SPI_OK;
    }

    osThreadId_t self = osThreadGetId();
    if (bus->lock_held)
    {
        if ((bus->lock_owner == self) && (bus->state == BSP_SPI_XFER_IDLE))
        {
            if (dbg)
            {
                dbg->h.last_status = BSP_SPI_OK;
                dbg->lock_last_fail = BSP_SPI_LOCK_FAIL_NONE;
                dbg->lock_ok++;
                dbg->lock_held = bus->lock_held;
                dbg->lock_owner = (uintptr_t)bus->lock_owner;
            }
            return BSP_SPI_OK;
        }
        if (timeout_ms == 0u)
        {
            if (dbg)
            {
                dbg->h.last_status = BSP_SPI_BUSY;
                dbg->lock_last_fail = BSP_SPI_LOCK_FAIL_ALREADY_HELD;
                dbg->lock_busy++;
                dbg->lock_held = bus->lock_held;
                dbg->lock_owner = (uintptr_t)bus->lock_owner;
            }
            return BSP_SPI_BUSY;
        }
    }

    if (!bus->sem)
    {
        if (dbg)
        {
            dbg->h.last_status = BSP_SPI_ERROR;
            dbg->lock_last_fail = BSP_SPI_LOCK_FAIL_SEM_NULL;
            dbg->lock_error++;
        }
        return BSP_SPI_ERROR;
    }

    uint32_t wait_ticks = bsp_spi_ms_to_ticks(timeout_ms);
    osStatus_t st = osSemaphoreAcquire(bus->sem, wait_ticks);
    if (dbg) dbg->lock_os_status = (uint32_t)st;
    if (st == osOK)
    {
        bus->lock_held = 1u;
        bus->lock_owner = self;
        if (dbg)
        {
            dbg->h.last_status = BSP_SPI_OK;
            dbg->lock_last_fail = BSP_SPI_LOCK_FAIL_NONE;
            dbg->lock_ok++;
            dbg->lock_held = bus->lock_held;
            dbg->lock_owner = (uintptr_t)bus->lock_owner;
        }
        return BSP_SPI_OK;
    }
    if (st == osErrorTimeout)
    {
        if (dbg)
        {
            dbg->h.last_status = BSP_SPI_BUSY;
            dbg->lock_last_fail = BSP_SPI_LOCK_FAIL_TIMEOUT;
            dbg->lock_busy++;
        }
        return BSP_SPI_BUSY;
    }
    if (st == osErrorResource)
    {
        if (dbg)
        {
            dbg->h.last_status = BSP_SPI_BUSY;
            dbg->lock_last_fail = BSP_SPI_LOCK_FAIL_OS_RESOURCE;
            dbg->lock_busy++;
        }
        return BSP_SPI_BUSY;
    }

    if (dbg)
    {
        dbg->h.last_status = BSP_SPI_ERROR;
        dbg->lock_last_fail = BSP_SPI_LOCK_FAIL_OS_ERROR;
        dbg->lock_error++;
    }
    return BSP_SPI_ERROR;
}

void bsp_spi_bus_release_isr(const bsp_spi_device_t *dev)
{
    if (!dev || !dev->bus) return;
    if (dev->bus->state != BSP_SPI_XFER_IDLE)
    {
        volatile omx_dbg_spi_t *dbg = bsp_spi_dbg_of(dev->bus);
        if (dbg) dbg->start_conflict++;
        bsp_spi_diag_set(dev->bus, BSP_SPI_DIAG_START_CONFLICT);
        return;
    }
    bsp_spi_bus_release_common(dev->bus);
}

/* ============================================================
 * Blocking wrapper (old)
 * ============================================================ */
bsp_spi_status_t bsp_spi_transfer_dma_blocking(bsp_spi_device_t *dev,
                                               const uint8_t *tx_buf,
                                               uint8_t *rx_buf,
                                               size_t len,
                                               uint32_t timeout_ms)
{
    return bsp_spi_transfer_ex(dev, tx_buf, rx_buf, len, NULL, NULL, 1u, timeout_ms);
}

/* ============================================================
 * Unified blocking/async transfer
 * ============================================================ */
static bsp_spi_status_t bsp_spi_transfer_ex_common(bsp_spi_device_t *dev,
                                                  const uint8_t *tx_buf,
                                                  uint8_t *rx_buf,
                                                  size_t len,
                                                  bsp_spi_callback_t cb,
                                                  void *user_ctx,
                                                  uint8_t blocking,
                                                  uint32_t timeout_ms,
                                                  uint8_t direct)
{
    bsp_spi_status_t lock_st = bsp_spi_bus_acquire(dev, timeout_ms);
    if (lock_st != BSP_SPI_OK) return lock_st;

    bsp_spi_status_t ret = bsp_spi_start_dma(dev,
                                             tx_buf,
                                             rx_buf,
                                             len,
                                             cb,
                                             user_ctx,
                                             direct,
                                             blocking);
    if (ret != BSP_SPI_OK)
    {
        bsp_spi_bus_release_isr(dev);
        return ret;
    }

    if (blocking)
    {
        bsp_spi_bus_t *bus = dev->bus;
        volatile omx_dbg_spi_t *dbg = bsp_spi_dbg_of(bus);
        uint32_t start = HAL_GetTick();
        uint32_t last_yield_tick = start;

        while (bus->state == BSP_SPI_XFER_BUSY)
        {
            uint32_t now = HAL_GetTick();
            if ((now - start) > timeout_ms)
            {
                if (dbg)
                {
                    dbg->xfer_timeout++;
                    dbg->h.last_tick = now;
                    dbg->state = bus->state;
                    dbg->hal_state = bus->hspi ? (uint32_t)HAL_SPI_GetState(bus->hspi) : 0u;
                    dbg->hal_error = bus->hspi ? (uint32_t)HAL_SPI_GetError(bus->hspi) : 0u;
                }
                (void)bsp_spi_recover_bus((bsp_spi_bus_id_t)bsp_spi_bus_index(bus),
                                          BSP_SPI_RECOVER_REASON_TIMEOUT);
                return BSP_SPI_BUSY;
            }
            bsp_spi_wait_yield(now, &last_yield_tick);
        }
        return bsp_spi_finish_blocking_completion(bus);
    }

    return BSP_SPI_OK;
}

bsp_spi_status_t bsp_spi_transfer_ex(bsp_spi_device_t *dev,
                                     const uint8_t *tx_buf,
                                     uint8_t *rx_buf,
                                     size_t len,
                                     bsp_spi_callback_t cb,
                                     void *user_ctx,
                                     uint8_t blocking,
                                     uint32_t timeout_ms)
{
    return bsp_spi_transfer_ex_common(dev, tx_buf, rx_buf, len, cb, user_ctx,
                                      blocking, timeout_ms, 0u);
}

bsp_spi_status_t bsp_spi_transfer_direct_ex(bsp_spi_device_t *dev,
                                            const uint8_t *tx_buf,
                                            uint8_t *rx_buf,
                                            size_t len,
                                            bsp_spi_callback_t cb,
                                            void *user_ctx,
                                            uint8_t blocking,
                                            uint32_t timeout_ms)
{
    return bsp_spi_transfer_ex_common(dev, tx_buf, rx_buf, len, cb, user_ctx,
                                      blocking, timeout_ms, 1u);
}

/* ============================================================
 * Check bus busy
 * ============================================================ */
bsp_spi_status_t bsp_spi_is_bus_busy(const bsp_spi_device_t *dev)
{
    if (!dev || !dev->bus) return BSP_SPI_INVALID_PARAM;
    return (dev->bus->state == BSP_SPI_XFER_IDLE) ? BSP_SPI_OK : BSP_SPI_BUSY;
}

bsp_spi_status_t bsp_spi_get_snapshot(bsp_spi_bus_id_t bus_id,
                                      bsp_spi_snapshot_t *out)
{
    if (!out)
    {
        return BSP_SPI_INVALID_PARAM;
    }

    bsp_spi_bus_t *bus = bsp_spi_get_bus_by_id(bus_id);
    if (!bus)
    {
        return BSP_SPI_INVALID_PARAM;
    }

    volatile omx_dbg_spi_t *dbg = bsp_spi_dbg_of(bus);
    uint32_t ps = __get_PRIMASK(); __disable_irq();
    out->state = (uint32_t)bus->state;
    out->hal_state = bus->hspi ? (uint32_t)HAL_SPI_GetState(bus->hspi) : 0u;
    out->hal_error = bus->hspi ? (uint32_t)HAL_SPI_GetError(bus->hspi) : 0u;
    out->lock_held = bus->lock_held;
    out->lock_owner = (uintptr_t)bus->lock_owner;
    out->current_dev = (uintptr_t)bus->current_dev;
    out->current_len = (uint32_t)bus->current_len;
    out->last_status = (uint32_t)bus->last_status;
    out->last_tick = HAL_GetTick();
    out->recover_count = dbg ? dbg->recover_count : 0u;
    out->cb_queued = bsp_spi_cb_queue_depth();
    out->cb_queue_overflow = dbg ? dbg->cb_queue_overflow : 0u;
    out->launch_guard = bus->launch_guard;
    out->pending_status = (uint32_t)bus->completion_pending;
    out->early_complete = dbg ? dbg->early_complete : 0u;
    out->deferred_publish = dbg ? dbg->deferred_publish : 0u;
    out->spurious_complete = dbg ? dbg->spurious_complete : 0u;
    out->pending_overwrite = dbg ? dbg->pending_overwrite : 0u;
    out->start_conflict = dbg ? dbg->start_conflict : 0u;
    out->last_hal_ret = dbg ? dbg->last_hal_ret : 0u;
    if (!ps) __enable_irq();

    return BSP_SPI_OK;
}

bsp_spi_status_t bsp_spi_recover_bus(bsp_spi_bus_id_t bus_id,
                                     uint32_t reason)
{
    if (__get_IPSR() != 0U)
    {
        return BSP_SPI_BUSY;
    }

    bsp_spi_bus_t *bus = bsp_spi_get_bus_by_id(bus_id);
    if (!bus || !bus->hspi)
    {
        return BSP_SPI_INVALID_PARAM;
    }

    volatile omx_dbg_spi_t *dbg = bsp_spi_dbg_of(bus);
    bsp_spi_device_t *dev = bus->current_dev;

    bsp_spi_log_pending_diag(bus);
    if (bsp_spi_can_log())
    {
        LOGW_SRC("BSP_SPI", "bus=%lu recovery requested reason=%lu state=%lu dev=%p",
                 (unsigned long)bus_id,
                 (unsigned long)reason,
                 (unsigned long)bus->state,
                 (void *)dev);
    }

    if (dev)
    {
        bsp_spi_cs_high(dev);
    }

    HAL_StatusTypeDef abort_st = HAL_SPI_Abort(bus->hspi);

    uint32_t ps = __get_PRIMASK(); __disable_irq();
    bus->last_status = BSP_SPI_ERROR;
    bsp_spi_clear_xfer(bus);
    if (dbg)
    {
        dbg->recover_count++;
        dbg->last_recover_reason = reason;
        dbg->h.last_tick = HAL_GetTick();
        dbg->state = bus->state;
        dbg->hal_state = (uint32_t)HAL_SPI_GetState(bus->hspi);
        dbg->hal_error = (uint32_t)HAL_SPI_GetError(bus->hspi);
        if (abort_st != HAL_OK)
        {
            dbg->recover_error++;
        }
    }
    if (!ps) __enable_irq();

    bsp_spi_bus_release_common(bus);
    if ((abort_st != HAL_OK) && bsp_spi_can_log())
    {
        LOGE_SRC("BSP_SPI", "bus=%lu HAL abort failed ret=%lu state=%lu error=0x%08lx",
                 (unsigned long)bus_id,
                 (unsigned long)abort_st,
                 (unsigned long)bus->hspi->State,
                 (unsigned long)bus->hspi->ErrorCode);
    }
    return (abort_st == HAL_OK) ? BSP_SPI_OK : BSP_SPI_ERROR;
}

/* ============================================================
 * HAL callbacks
 * ============================================================ */
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    bsp_spi_bus_t *bus = bsp_spi_get_bus_by_handle(hspi);
    if (!bus) return;
    volatile omx_dbg_spi_t *dbg = bsp_spi_dbg_of(bus);

    if ((bus->state != BSP_SPI_XFER_BUSY) || (bus->current_dev == NULL))
    {
        if (dbg) dbg->spurious_complete++;
        bsp_spi_diag_set(bus, BSP_SPI_DIAG_SPURIOUS_COMPLETE);
        return;
    }

    bsp_spi_device_t *dev = bus->current_dev;
    if (dev) bsp_spi_cs_high(dev);
    bsp_spi_copy_rx_if_needed(bus);

    uint32_t ps = __get_PRIMASK(); __disable_irq();
    if (bus->launch_guard != 0u)
    {
        if (bus->completion_pending == BSP_SPI_PENDING_NONE)
        {
            bus->completion_pending = BSP_SPI_PENDING_OK;
            if (dbg)
            {
                dbg->early_complete++;
                dbg->pending_status = BSP_SPI_PENDING_OK;
            }
        }
        else
        {
            if (dbg) dbg->pending_overwrite++;
            bus->completion_pending = BSP_SPI_PENDING_ERROR;
            if (dbg) dbg->pending_status = BSP_SPI_PENDING_ERROR;
            bsp_spi_diag_set(bus, BSP_SPI_DIAG_PENDING_OVERWRITE);
        }
        if (!ps) __enable_irq();
        return;
    }
    if (!ps) __enable_irq();

    bsp_spi_publish_completion(bus, BSP_SPI_PENDING_OK);
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    HAL_SPI_TxRxCpltCallback(hspi);
}

void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    HAL_SPI_TxRxCpltCallback(hspi);
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    bsp_spi_bus_t *bus = bsp_spi_get_bus_by_handle(hspi);
    if (!bus) return;
    volatile omx_dbg_spi_t *dbg = bsp_spi_dbg_of(bus);

    if ((bus->state != BSP_SPI_XFER_BUSY) || (bus->current_dev == NULL))
    {
        if (dbg) dbg->spurious_complete++;
        bsp_spi_diag_set(bus, BSP_SPI_DIAG_SPURIOUS_COMPLETE);
        return;
    }

    bsp_spi_device_t *dev = bus->current_dev;
    if (dev) bsp_spi_cs_high(dev);

    uint32_t ps = __get_PRIMASK(); __disable_irq();
    if (bus->launch_guard != 0u)
    {
        if (bus->completion_pending != BSP_SPI_PENDING_NONE)
        {
            if (dbg) dbg->pending_overwrite++;
            bsp_spi_diag_set(bus, BSP_SPI_DIAG_PENDING_OVERWRITE);
        }
        else if (dbg)
        {
            dbg->early_complete++;
        }
        bus->completion_pending = BSP_SPI_PENDING_ERROR;
        if (dbg) dbg->pending_status = BSP_SPI_PENDING_ERROR;
        if (!ps) __enable_irq();
        return;
    }
    if (!ps) __enable_irq();

    bsp_spi_publish_completion(bus, BSP_SPI_PENDING_ERROR);
}
