//
// Created by sirin on 2025/10/4.
//
#include "SirinEmbedOS/IO/CAN/Native/bsp_fdcan.h"
#include <string.h>

#include "SirinEmbedOS/Core/Memory/mem_sections.h"

/* Board owns fixed HAL instance binding. */
extern FDCAN_HandleTypeDef *SRNOS_BoardCANHandle(uint8_t bus);
static FDCAN_HandleTypeDef *_handle(uint8_t bus) { return SRNOS_BoardCANHandle(bus); }

static inline uint32_t _now_us(void) {
    /* Use HAL ticks if DWT not enabled. Replace with DWT->CYCCNT/scaler if needed. */
    return HAL_GetTick() * 1000u;
}

/* DLC to bytes per ISO11898-1 CAN-FD table */
uint8_t bsp_fdcan_dlc_to_len(uint32_t dlc) {
    static const uint8_t lut[16] =
        {0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64};
    dlc &= 0xF;
    return lut[dlc];
}

/* ==== RX rings (one per bus) ==== */
typedef struct {
    bsp_fdcan_rx_frame_t buf[BSP_FDCAN_RX_RING_SIZE];
    volatile uint16_t     w;  /* ISR writer */
    volatile uint16_t     r;  /* task reader */
} rx_ring_t;

static rx_ring_t s_rx1 OMX_DTCM_BSS;
static rx_ring_t s_rx2 OMX_DTCM_BSS;
static rx_ring_t s_rx3 OMX_DTCM_BSS;

static inline rx_ring_t* _ring(uint8_t bus) {
    switch (bus) {
        case 1: return &s_rx1;
        case 2: return &s_rx2;
        case 3: return &s_rx3;
        default: return NULL;
    }
}

/* ==== Diagnostics ==== */
volatile bsp_fdcan_diag_t g_bsp_fdcan_diag[BSP_FDCAN_BUS_COUNT] = {0};

bsp_fdcan_diag_t bsp_fdcan_get_diag(uint8_t bus) {
    bsp_fdcan_diag_t z = {0};
    if (bus >= 1 && bus <= BSP_FDCAN_BUS_COUNT) {
        __IO bsp_fdcan_diag_t *d = &g_bsp_fdcan_diag[bus-1];
        z = *d;
    }
    return z;
}

/* ==== Public: init/start ==== */

static bool _disable_filter_slots(FDCAN_HandleTypeDef* h) {
    FDCAN_FilterTypeDef f = {0};
    f.FilterType = FDCAN_FILTER_MASK;
    f.FilterConfig = FDCAN_FILTER_DISABLE;

    f.IdType = FDCAN_STANDARD_ID;
    for (uint32_t i = 0; i < h->Init.StdFiltersNbr; ++i) {
        f.FilterIndex = i;
        if (HAL_FDCAN_ConfigFilter(h, &f) != HAL_OK) {
            return false;
        }
    }

    f.IdType = FDCAN_EXTENDED_ID;
    for (uint32_t i = 0; i < h->Init.ExtFiltersNbr; ++i) {
        f.FilterIndex = i;
        if (HAL_FDCAN_ConfigFilter(h, &f) != HAL_OK) {
            return false;
        }
    }

    return true;
}

static bool _configure_accept_all(FDCAN_HandleTypeDef* h) {
    if (h == NULL || h->State != HAL_FDCAN_STATE_READY) {
        return false;
    }

    if (!_disable_filter_slots(h)) {
        return false;
    }

    return HAL_FDCAN_ConfigGlobalFilter(h,
                                        FDCAN_ACCEPT_IN_RX_FIFO0,
                                        FDCAN_ACCEPT_IN_RX_FIFO0,
                                        FDCAN_FILTER_REMOTE,
                                        FDCAN_FILTER_REMOTE) == HAL_OK;
}

bool bsp_fdcan_init_all(void) {
    bool ok = true;
    for (uint8_t bus = 1u; bus <= BSP_FDCAN_BUS_COUNT; ++bus) {
        if (!_configure_accept_all(_handle(bus))) {
            ok = false;
        }
    }
    return ok;
}

static bool _activate_notifications(FDCAN_HandleTypeDef* h) {
    uint32_t its = 0;
    its |= FDCAN_IT_RX_FIFO0_NEW_MESSAGE;
#if BSP_FDCAN_ENABLE_RXFIFO1
    its |= FDCAN_IT_RX_FIFO1_NEW_MESSAGE;
#endif
    its |= FDCAN_IT_TX_EVT_FIFO_NEW_DATA;
    its |= FDCAN_IT_BUS_OFF;
    its |= FDCAN_IT_RAM_WATCHDOG;
    its |= FDCAN_IT_ERROR_PASSIVE;
    return HAL_FDCAN_ActivateNotification(h, its, UINT32_MAX) == HAL_OK;
}

bool bsp_fdcan_start(uint8_t bus) {
    FDCAN_HandleTypeDef* h = _handle(bus);
    if (!h) return false;

    if (h->State == HAL_FDCAN_STATE_BUSY) {
        return _activate_notifications(h);
    }
    if (h->State != HAL_FDCAN_STATE_READY) {
        return false;
    }

    if (!_activate_notifications(h)) return false;
    if (HAL_FDCAN_Start(h) != HAL_OK) return false;
    return true;
}

bool bsp_fdcan_start_all(void) {
    bool ok = true;
    for (uint8_t bus = 1u; bus <= BSP_FDCAN_BUS_COUNT; ++bus) {
        if (!bsp_fdcan_start(bus)) {
            ok = false;
        }
    }
    return ok;
}

void bsp_fdcan_stop(uint8_t bus) {
    FDCAN_HandleTypeDef* h = _handle(bus);
    if (!h) return;
    (void)HAL_FDCAN_Stop(h);
}

/* ==== Public: TX (non-blocking) ==== */

bool bsp_fdcan_try_tx(uint8_t bus,
                      const FDCAN_TxHeaderTypeDef* tx_hdr,
                      const uint8_t* payload)
{
    FDCAN_HandleTypeDef* h = _handle(bus);
    if (!h) return false;

    /* Quick check: if no free element, return immediately */
    if (HAL_FDCAN_GetTxFifoFreeLevel(h) == 0) {
        g_bsp_fdcan_diag[bus-1].tx_busy_cnt++;
        return false;
    }
    if (HAL_FDCAN_AddMessageToTxFifoQ(h, (FDCAN_TxHeaderTypeDef*)tx_hdr,
                                      (uint8_t*)payload) == HAL_OK) {
        g_bsp_fdcan_diag[bus-1].tx_ok_cnt++;
        return true;
    } else {
        g_bsp_fdcan_diag[bus-1].tx_busy_cnt++;
        return false;
    }
}

/* ==== Public: RX pop (task context) ==== */

size_t bsp_fdcan_rx_pop(uint8_t bus,
                        bsp_fdcan_rx_frame_t *out,
                        size_t max_count)
{
    rx_ring_t* ring = _ring(bus);
    if (!ring || !out || max_count == 0) return 0;

    size_t n = 0;
    uint16_t r = ring->r;
    uint16_t w = ring->w;

    while ((r != w) && (n < max_count)) {
        out[n] = ring->buf[r];
        r = (uint16_t)((r + 1u) % BSP_FDCAN_RX_RING_SIZE);
        n++;
    }
    ring->r = r;
    return n;
}

/* ==== Public: runtime filter append ==== */

static HAL_StatusTypeDef _config_filter_common(FDCAN_HandleTypeDef* h,
                                               FDCAN_FilterTypeDef* f)
{
    /* Keep everything CubeMX configured; only append additional entries. */
    return HAL_FDCAN_ConfigFilter(h, f);
}

HAL_StatusTypeDef bsp_fdcan_add_std_filter(uint8_t bus,
                                           uint16_t id,
                                           uint16_t mask,
                                           uint8_t  fifo)
{
    FDCAN_HandleTypeDef* h = _handle(bus);
    if (!h) return HAL_ERROR;

    FDCAN_FilterTypeDef f = {0};
    f.IdType      = FDCAN_STANDARD_ID;
    f.FilterIndex = 0; /* HAL will place at next free index if enabled by MX config */
    f.FilterType  = FDCAN_FILTER_MASK;
    f.FilterConfig= (fifo==0) ? FDCAN_FILTER_TO_RXFIFO0 : FDCAN_FILTER_TO_RXFIFO1;
    f.FilterID1   = id;
    f.FilterID2   = mask;

    return _config_filter_common(h, &f);
}

HAL_StatusTypeDef bsp_fdcan_add_ext_filter(uint8_t bus,
                                           uint32_t id,
                                           uint32_t mask,
                                           uint8_t  fifo)
{
    FDCAN_HandleTypeDef* h = _handle(bus);
    if (!h) return HAL_ERROR;

    FDCAN_FilterTypeDef f = {0};
    f.IdType      = FDCAN_EXTENDED_ID;
    f.FilterIndex = 0;
    f.FilterType  = FDCAN_FILTER_MASK;
    f.FilterConfig= (fifo==0) ? FDCAN_FILTER_TO_RXFIFO0 : FDCAN_FILTER_TO_RXFIFO1;
    f.FilterID1   = id;
    f.FilterID2   = mask;

    return _config_filter_common(h, &f);
}

/* ==== ISR helpers ==== */

OMX_ITCM_TEXT
static void _rx_from_fifo(FDCAN_HandleTypeDef* h, uint8_t bus, uint32_t fifo) {
    rx_ring_t* ring = _ring(bus);
    if (!ring) return;

    /* Drain all pending messages in the FIFO */
    while (HAL_FDCAN_GetRxFifoFillLevel(h, fifo) > 0) {
        uint16_t next_w = (uint16_t)((ring->w + 1u) % BSP_FDCAN_RX_RING_SIZE);
        if (next_w == ring->r) {
            /* overrun: drop oldest by advancing r */
            ring->r = (uint16_t)((ring->r + 1u) % BSP_FDCAN_RX_RING_SIZE);
            g_bsp_fdcan_diag[bus-1].rx_overrun_cnt++;
        }
        bsp_fdcan_rx_frame_t* dst = &ring->buf[ring->w];
        if (fifo == FDCAN_RX_FIFO0) {
            (void)HAL_FDCAN_GetRxMessage(h, FDCAN_RX_FIFO0, &dst->hdr, dst->data);
        } else {
            (void)HAL_FDCAN_GetRxMessage(h, FDCAN_RX_FIFO1, &dst->hdr, dst->data);
        }
        dst->ts_us = _now_us();
        ring->w = next_w;
        g_bsp_fdcan_diag[bus-1].rx_isr_cnt++;
    }

    /* Light hook for upper layer (e.g., task notify) */
    bsp_fdcan_on_rx_isr(bus);
}

/* ==== HAL callbacks (ISR context) ==== */

static uint8_t _bus_of(FDCAN_HandleTypeDef* h) {
    if (h == _handle(1)) return 1;
    if (h == _handle(2)) return 2;
    if (h == _handle(3)) return 3;
    return 0;
}

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *h, uint32_t RxFifo0ITs) {
    (void)RxFifo0ITs;
    uint8_t bus = _bus_of(h);
    if (bus) _rx_from_fifo(h, bus, FDCAN_RX_FIFO0);
}

#if BSP_FDCAN_ENABLE_RXFIFO1
void HAL_FDCAN_RxFifo1Callback(FDCAN_HandleTypeDef *h, uint32_t RxFifo1ITs) {
    (void)RxFifo1ITs;
    uint8_t bus = _bus_of(h);
    if (bus) _rx_from_fifo(h, bus, FDCAN_RX_FIFO1);
}
#endif

void HAL_FDCAN_TxBufferCompleteCallback(FDCAN_HandleTypeDef *h, uint32_t BufferIndexes) {
    (void)BufferIndexes;
    uint8_t bus = _bus_of(h);
    if (bus) bsp_fdcan_on_tx_event(bus);
}

void HAL_FDCAN_TxEventFifoCallback(FDCAN_HandleTypeDef *h, uint32_t TxEventFifoITs) {
    (void)TxEventFifoITs;
    uint8_t bus = _bus_of(h);
    if (!bus) return;

    while ((h->Instance->TXEFS & FDCAN_TXEFS_EFFL_Msk) != 0u) {
        FDCAN_TxEventFifoTypeDef event;
        if (HAL_FDCAN_GetTxEvent(h, &event) != HAL_OK) {
            break;
        }
        bsp_fdcan_on_tx_event(bus);
    }
}

void HAL_FDCAN_ErrorCallback(FDCAN_HandleTypeDef *h) {
    uint8_t bus = _bus_of(h);
    if (!bus) return;

    uint32_t lec = (h->Instance->PSR & FDCAN_PSR_LEC_Msk) >> FDCAN_PSR_LEC_Pos;
    g_bsp_fdcan_diag[bus-1].last_err_code = lec;

    FDCAN_ProtocolStatusTypeDef ps;
    if (HAL_FDCAN_GetProtocolStatus(h, &ps) == HAL_OK) {
        /* ps.BusOff == 1 则处于 Bus-Off */
        if (ps.BusOff) {
            g_bsp_fdcan_diag[bus-1].bus_off_cnt++;
            bsp_fdcan_on_busoff(bus);
        }
    }
}

/* ==== TX convenience wrappers (classic CAN, 0..8B, RTOS-free) ==== */

// len -> DLC for classic CAN (0..8)
static inline uint32_t _len_to_dlc_classic(uint8_t len) {
    if (len > 8u) len = 8u;
    switch (len) {
        case 0:  return FDCAN_DLC_BYTES_0;
        case 1:  return FDCAN_DLC_BYTES_1;
        case 2:  return FDCAN_DLC_BYTES_2;
        case 3:  return FDCAN_DLC_BYTES_3;
        case 4:  return FDCAN_DLC_BYTES_4;
        case 5:  return FDCAN_DLC_BYTES_5;
        case 6:  return FDCAN_DLC_BYTES_6;
        case 7:  return FDCAN_DLC_BYTES_7;
        default: return FDCAN_DLC_BYTES_8;
    }
}

static bool _try_add_tx(FDCAN_HandleTypeDef* h,
                        const FDCAN_TxHeaderTypeDef* hdr,
                        const uint8_t* payload,
                        uint8_t bus)
{
    if (!h) return false;

    if (HAL_FDCAN_GetTxFifoFreeLevel(h) == 0) {
        g_bsp_fdcan_diag[bus-1].tx_busy_cnt++;
        return false;
    }
    if (HAL_FDCAN_AddMessageToTxFifoQ(h,
            (FDCAN_TxHeaderTypeDef*)hdr, (uint8_t*)payload) == HAL_OK) {
        g_bsp_fdcan_diag[bus-1].tx_ok_cnt++;
        return true;
    } else {
        g_bsp_fdcan_diag[bus-1].tx_busy_cnt++;
        return false;
    }
}

bool bsp_fdcan_tx_push(uint8_t bus, uint16_t std_id, const uint8_t* data, uint8_t len)
{
    FDCAN_HandleTypeDef* h = _handle(bus);
    if (!h || !data) return false;

    FDCAN_TxHeaderTypeDef tx = {0};
    tx.Identifier          = std_id;
    tx.IdType              = FDCAN_STANDARD_ID;
    tx.TxFrameType         = FDCAN_DATA_FRAME;
    tx.DataLength          = _len_to_dlc_classic(len);
    tx.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    tx.BitRateSwitch       = FDCAN_BRS_OFF;
    tx.FDFormat            = FDCAN_CLASSIC_CAN;
    tx.TxEventFifoControl  = FDCAN_STORE_TX_EVENTS;
    tx.MessageMarker       = 0;

    return _try_add_tx(h, &tx, data, bus);
}

bool bsp_fdcan_tx_push_ext(uint8_t bus, uint32_t ext_id, const uint8_t* data, uint8_t len)
{
    FDCAN_HandleTypeDef* h = _handle(bus);
    if (!h || !data) return false;

    FDCAN_TxHeaderTypeDef tx = {0};
    tx.Identifier          = (ext_id & 0x1FFFFFFFu);
    tx.IdType              = FDCAN_EXTENDED_ID;
    tx.TxFrameType         = FDCAN_DATA_FRAME;
    tx.DataLength          = _len_to_dlc_classic(len);
    tx.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    tx.BitRateSwitch       = FDCAN_BRS_OFF;
    tx.FDFormat            = FDCAN_CLASSIC_CAN;
    tx.TxEventFifoControl  = FDCAN_STORE_TX_EVENTS;
    tx.MessageMarker       = 0;

    return _try_add_tx(h, &tx, data, bus);
}


/* ==== Weak hooks default (no-op) ==== */

void __attribute__((weak)) bsp_fdcan_on_rx_isr(uint8_t bus) {
    (void)bus;
}

void __attribute__((weak)) bsp_fdcan_on_busoff(uint8_t bus) {
    (void)bus;
}

void __attribute__((weak)) bsp_fdcan_on_tx_event(uint8_t bus) {
    (void)bus;
}
