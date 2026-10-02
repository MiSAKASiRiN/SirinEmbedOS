//
// Created by sirin on 2025/10/4.
//
#include "IO/CAN/Native/fdcan_internal.h"
#include <string.h>

#include "SirinEmbedOS/Core/Memory/MemorySections.h"

/* Board owns fixed HAL instance binding. */
#include "IO/can_board.h"
static FDCAN_HandleTypeDef *_handle(uint8_t bus) { return SRNOS_BoardCANHandle(bus); }

static inline uint32_t _now_us(void) {
    /* Use HAL ticks if DWT not enabled. Replace with DWT->CYCCNT/scaler if needed. */
    return HAL_GetTick() * 1000u;
}

/* DLC to bytes per ISO11898-1 CAN-FD table */
uint8_t SRNOS_FDCANDlcToLen(uint32_t dlc) {
    static const uint8_t lut[16] =
        {0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64};
    dlc &= 0xF;
    return lut[dlc];
}

/* ==== RX rings (one per bus) ==== */
typedef struct {
    SRNOS_FDCANRxFrame buf[SRNOS_FDCAN_RX_RING_SIZE];
    volatile uint16_t     w;  /* ISR writer */
    volatile uint16_t     r;  /* task reader */
} rx_ring_t;

static rx_ring_t s_rx1 SRNOS_DTCM_BSS;
static rx_ring_t s_rx2 SRNOS_DTCM_BSS;
static rx_ring_t s_rx3 SRNOS_DTCM_BSS;

static inline rx_ring_t* _ring(uint8_t bus) {
    switch (bus) {
        case 1: return &s_rx1;
        case 2: return &s_rx2;
        case 3: return &s_rx3;
        default: return NULL;
    }
}

/* ==== Diagnostics ==== */
volatile SRNOS_FDCANDiag SRNOS_FDCANDiagnostics[SRNOS_FDCAN_BUS_COUNT] = {0};

SRNOS_FDCANDiag SRNOS_FDCANGetDiag(uint8_t bus) {
    SRNOS_FDCANDiag z = {0};
    if (bus >= 1 && bus <= SRNOS_FDCAN_BUS_COUNT) {
        __IO SRNOS_FDCANDiag *d = &SRNOS_FDCANDiagnostics[bus-1];
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

bool SRNOS_FDCANInitAll(void) {
    bool ok = true;
    for (uint8_t bus = 1u; bus <= SRNOS_FDCAN_BUS_COUNT; ++bus) {
        if (!_configure_accept_all(_handle(bus))) {
            ok = false;
        }
    }
    return ok;
}

static bool _activate_notifications(FDCAN_HandleTypeDef* h) {
    uint32_t its = 0;
    its |= FDCAN_IT_RX_FIFO0_NEW_MESSAGE;
#if SRNOS_FDCAN_ENABLE_RXFIFO1
    its |= FDCAN_IT_RX_FIFO1_NEW_MESSAGE;
#endif
    its |= FDCAN_IT_TX_EVT_FIFO_NEW_DATA;
    its |= FDCAN_IT_BUS_OFF;
    its |= FDCAN_IT_RAM_WATCHDOG;
    its |= FDCAN_IT_ERROR_PASSIVE;
    return HAL_FDCAN_ActivateNotification(h, its, UINT32_MAX) == HAL_OK;
}

bool SRNOS_FDCANStart(uint8_t bus) {
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

bool SRNOS_FDCANStartAll(void) {
    bool ok = true;
    for (uint8_t bus = 1u; bus <= SRNOS_FDCAN_BUS_COUNT; ++bus) {
        if (!SRNOS_FDCANStart(bus)) {
            ok = false;
        }
    }
    return ok;
}

void SRNOS_FDCANStop(uint8_t bus) {
    FDCAN_HandleTypeDef* h = _handle(bus);
    if (!h) return;
    (void)HAL_FDCAN_Stop(h);
}

/* ==== Public: TX (non-blocking) ==== */

bool SRNOS_FDCANTryTx(uint8_t bus,
                      const FDCAN_TxHeaderTypeDef* tx_hdr,
                      const uint8_t* payload)
{
    FDCAN_HandleTypeDef* h = _handle(bus);
    if (!h) return false;

    /* Quick check: if no free element, return immediately */
    if (HAL_FDCAN_GetTxFifoFreeLevel(h) == 0) {
        SRNOS_FDCANDiagnostics[bus-1].tx_busy_cnt++;
        return false;
    }
    if (HAL_FDCAN_AddMessageToTxFifoQ(h, (FDCAN_TxHeaderTypeDef*)tx_hdr,
                                      (uint8_t*)payload) == HAL_OK) {
        SRNOS_FDCANDiagnostics[bus-1].tx_ok_cnt++;
        return true;
    } else {
        SRNOS_FDCANDiagnostics[bus-1].tx_busy_cnt++;
        return false;
    }
}

/* ==== Public: RX pop (task context) ==== */

size_t SRNOS_FDCANRxPop(uint8_t bus,
                        SRNOS_FDCANRxFrame *out,
                        size_t max_count)
{
    rx_ring_t* ring = _ring(bus);
    if (!ring || !out || max_count == 0) return 0;

    size_t n = 0;
    uint16_t r = ring->r;
    uint16_t w = ring->w;

    while ((r != w) && (n < max_count)) {
        out[n] = ring->buf[r];
        r = (uint16_t)((r + 1u) % SRNOS_FDCAN_RX_RING_SIZE);
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

HAL_StatusTypeDef SRNOS_FDCANAddStdFilter(uint8_t bus,
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

HAL_StatusTypeDef SRNOS_FDCANAddExtFilter(uint8_t bus,
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

SRNOS_ITCM_TEXT
static void _rx_from_fifo(FDCAN_HandleTypeDef* h, uint8_t bus, uint32_t fifo) {
    rx_ring_t* ring = _ring(bus);
    if (!ring) return;

    /* Drain all pending messages in the FIFO */
    while (HAL_FDCAN_GetRxFifoFillLevel(h, fifo) > 0) {
        uint16_t next_w = (uint16_t)((ring->w + 1u) % SRNOS_FDCAN_RX_RING_SIZE);
        if (next_w == ring->r) {
            /* overrun: drop oldest by advancing r */
            ring->r = (uint16_t)((ring->r + 1u) % SRNOS_FDCAN_RX_RING_SIZE);
            SRNOS_FDCANDiagnostics[bus-1].rx_overrun_cnt++;
        }
        SRNOS_FDCANRxFrame* dst = &ring->buf[ring->w];
        if (fifo == FDCAN_RX_FIFO0) {
            (void)HAL_FDCAN_GetRxMessage(h, FDCAN_RX_FIFO0, &dst->hdr, dst->data);
        } else {
            (void)HAL_FDCAN_GetRxMessage(h, FDCAN_RX_FIFO1, &dst->hdr, dst->data);
        }
        dst->ts_us = _now_us();
        ring->w = next_w;
        SRNOS_FDCANDiagnostics[bus-1].rx_isr_cnt++;
    }

    /* Light hook for upper layer (e.g., task notify) */
    SRNOS_FDCANOnRxISR(bus);
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

#if SRNOS_FDCAN_ENABLE_RXFIFO1
void HAL_FDCAN_RxFifo1Callback(FDCAN_HandleTypeDef *h, uint32_t RxFifo1ITs) {
    (void)RxFifo1ITs;
    uint8_t bus = _bus_of(h);
    if (bus) _rx_from_fifo(h, bus, FDCAN_RX_FIFO1);
}
#endif

void HAL_FDCAN_TxBufferCompleteCallback(FDCAN_HandleTypeDef *h, uint32_t BufferIndexes) {
    (void)BufferIndexes;
    uint8_t bus = _bus_of(h);
    if (bus) SRNOS_FDCANOnTxEvent(bus);
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
        SRNOS_FDCANOnTxEvent(bus);
    }
}

void HAL_FDCAN_ErrorCallback(FDCAN_HandleTypeDef *h) {
    uint8_t bus = _bus_of(h);
    if (!bus) return;

    uint32_t lec = (h->Instance->PSR & FDCAN_PSR_LEC_Msk) >> FDCAN_PSR_LEC_Pos;
    SRNOS_FDCANDiagnostics[bus-1].last_err_code = lec;

    FDCAN_ProtocolStatusTypeDef ps;
    if (HAL_FDCAN_GetProtocolStatus(h, &ps) == HAL_OK) {
        /* ps.BusOff == 1 则处于 Bus-Off */
        if (ps.BusOff) {
            SRNOS_FDCANDiagnostics[bus-1].bus_off_cnt++;
            SRNOS_FDCANOnBusoff(bus);
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
        SRNOS_FDCANDiagnostics[bus-1].tx_busy_cnt++;
        return false;
    }
    if (HAL_FDCAN_AddMessageToTxFifoQ(h,
            (FDCAN_TxHeaderTypeDef*)hdr, (uint8_t*)payload) == HAL_OK) {
        SRNOS_FDCANDiagnostics[bus-1].tx_ok_cnt++;
        return true;
    } else {
        SRNOS_FDCANDiagnostics[bus-1].tx_busy_cnt++;
        return false;
    }
}

bool SRNOS_FDCANTxPush(uint8_t bus, uint16_t std_id, const uint8_t* data, uint8_t len)
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

bool SRNOS_FDCANTxPushExt(uint8_t bus, uint32_t ext_id, const uint8_t* data, uint8_t len)
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

void __attribute__((weak)) SRNOS_FDCANOnRxISR(uint8_t bus) {
    (void)bus;
}

void __attribute__((weak)) SRNOS_FDCANOnBusoff(uint8_t bus) {
    (void)bus;
}

void __attribute__((weak)) SRNOS_FDCANOnTxEvent(uint8_t bus) {
    (void)bus;
}
