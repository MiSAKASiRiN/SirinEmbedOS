//
// Created by sirin on 2025/10/4.
//
#include "IO/CAN/Native/fdcan_internal.h"
#include "IO/CAN/can_controller_internal.h"
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
    uint16_t w; /* ISR is the only writer; release-publishes complete frames. */
    uint16_t r; /* Worker is the only writer; release-publishes consumed slots. */
} rx_ring_t;

_Static_assert(SRNOS_FDCAN_RX_RING_SIZE > 1u && SRNOS_FDCAN_RX_RING_SIZE <= UINT16_MAX, "RX ring index range");
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

enum { SRNOS_FDCAN_RECOVERY_HEALTHY, SRNOS_FDCAN_RECOVERY_BACKOFF,
       SRNOS_FDCAN_RECOVERY_ABORT, SRNOS_FDCAN_RECOVERY_VERIFY, SRNOS_FDCAN_RECOVERY_FAULT };
typedef struct {
    uint32_t pending, state, deadline_ms, attempts;
} recovery_t;
static recovery_t s_recovery[SRNOS_FDCAN_BUS_COUNT];
static bool s_init_attempted;
static bool s_config_applied[SRNOS_FDCAN_BUS_COUNT];
static bool reached(uint32_t now,uint32_t due) { return (int32_t)(now-due)>=0; }
static void recovery_state(uint8_t bus,uint32_t state)
{
    __atomic_store_n(&s_recovery[bus-1u].state,state,__ATOMIC_RELEASE);
    SRNOS_FDCANDiagnostics[bus-1u].recovery_state=state;
}
bool SRNOS_FDCANIsReady(uint8_t bus)
{
    FDCAN_HandleTypeDef *h=_handle(bus);
    if(!h || bus<1u || bus>SRNOS_FDCAN_BUS_COUNT ||
       !__atomic_load_n(&s_config_applied[bus-1u],__ATOMIC_ACQUIRE)) return false;
    recovery_t *r=&s_recovery[bus-1u];
    return __atomic_load_n(&r->state,__ATOMIC_ACQUIRE)==SRNOS_FDCAN_RECOVERY_HEALTHY &&
        !__atomic_load_n(&r->pending,__ATOMIC_ACQUIRE) && h->State==HAL_FDCAN_STATE_BUSY &&
        !(h->Instance->PSR & FDCAN_PSR_BO) && !(h->Instance->CCCR & FDCAN_CCCR_INIT);
}
static void recovery_failed(uint8_t bus,uint32_t now)
{
    recovery_t *r=&s_recovery[bus-1u];
    ++SRNOS_FDCANDiagnostics[bus-1u].recovery_errors;
    if(r->attempts>=3u) {
        ++SRNOS_FDCANDiagnostics[bus-1u].recovery_faults;
        recovery_state(bus,SRNOS_FDCAN_RECOVERY_FAULT);
    } else {
        r->deadline_ms=now+100u;
        recovery_state(bus,SRNOS_FDCAN_RECOVERY_BACKOFF);
    }
}
uint32_t SRNOS_FDCANNextServiceWait(uint8_t bus,uint32_t now)
{
    if(bus<1u || bus>SRNOS_FDCAN_BUS_COUNT) return UINT32_MAX;
    recovery_t *r=&s_recovery[bus-1u];
    if(__atomic_load_n(&r->pending,__ATOMIC_ACQUIRE)) return 0;
    uint32_t state=__atomic_load_n(&r->state,__ATOMIC_ACQUIRE);
    if(state==SRNOS_FDCAN_RECOVERY_HEALTHY || state==SRNOS_FDCAN_RECOVERY_FAULT) return UINT32_MAX;
    if(reached(now,r->deadline_ms)) return 0;
    uint32_t remaining=r->deadline_ms-now;
    /* Abort acknowledgement has no required enabled interrupt. */
    return state==SRNOS_FDCAN_RECOVERY_ABORT && remaining>1u?1u:remaining;
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
    if (s_init_attempted) {
        for (uint8_t i = 0u; i < SRNOS_FDCAN_BUS_COUNT; ++i) ok = ok && s_config_applied[i];
        return ok;
    }
    s_init_attempted = true;
    for (uint8_t bus = 1u; bus <= SRNOS_FDCAN_BUS_COUNT; ++bus) {
        SRNOS_CANControllerConfig config;
        const bool applied = SRNOS_CANControllerGetConfig(bus, &config) &&
                             SRNOS_BoardCANApplyConfig(bus, &config) &&
                             _configure_accept_all(_handle(bus));
        __atomic_store_n(&s_config_applied[bus-1u], applied, __ATOMIC_RELEASE);
        if (!applied) ok = false;
    }
    return ok;
}

static bool _activate_notifications(FDCAN_HandleTypeDef* h) {
    uint32_t its = 0;
    its |= FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_RX_FIFO0_MESSAGE_LOST;
#if SRNOS_FDCAN_ENABLE_RXFIFO1
    its |= FDCAN_IT_RX_FIFO1_NEW_MESSAGE | FDCAN_IT_RX_FIFO1_MESSAGE_LOST;
#endif
    its |= FDCAN_IT_TX_EVT_FIFO_NEW_DATA;
    its |= FDCAN_IT_BUS_OFF;
    its |= FDCAN_IT_RAM_WATCHDOG;
    its |= FDCAN_IT_ERROR_PASSIVE;
    return HAL_FDCAN_ActivateNotification(h, its, UINT32_MAX) == HAL_OK;
}

bool SRNOS_FDCANStart(uint8_t bus) {
    FDCAN_HandleTypeDef* h = _handle(bus);
    if (!h || bus < 1u || bus > SRNOS_FDCAN_BUS_COUNT ||
        !__atomic_load_n(&s_config_applied[bus-1u],__ATOMIC_ACQUIRE)) return false;

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

/* Called only by the TX worker. HAL_Stop has two register acknowledgement
 * loops bounded by FDCAN_TIMEOUT_COUNT (50 in the pinned HAL); HAL_Start has
 * no wait. Hardware cancellation and bus integration are checked on later
 * worker passes, never by an ISR or a busy-wait loop. */
bool SRNOS_FDCANService(uint8_t bus,uint32_t now)
{
    FDCAN_HandleTypeDef *h=_handle(bus);
    if(!h || bus<1u || bus>SRNOS_FDCAN_BUS_COUNT ||
       !__atomic_load_n(&s_config_applied[bus-1u],__ATOMIC_ACQUIRE)) return false;
    recovery_t *r=&s_recovery[bus-1u];
    if(__atomic_exchange_n(&r->pending,0u,__ATOMIC_ACQ_REL) &&
       r->state==SRNOS_FDCAN_RECOVERY_HEALTHY) {
        r->attempts=0;
        r->deadline_ms=now+100u;
        recovery_state(bus,SRNOS_FDCAN_RECOVERY_BACKOFF);
    }
    if(r->state==SRNOS_FDCAN_RECOVERY_HEALTHY) return SRNOS_FDCANIsReady(bus);
    if(r->state==SRNOS_FDCAN_RECOVERY_FAULT) return false;
    if(r->state==SRNOS_FDCAN_RECOVERY_BACKOFF) {
        if(!reached(now,r->deadline_ms)) return false;
        ++r->attempts;
        ++SRNOS_FDCANDiagnostics[bus-1u].recovery_attempts;
        /* Never replay a pre-fault hardware command after reconnecting. */
        uint32_t pending=h->Instance->TXBRP;
        if(pending && HAL_FDCAN_AbortTxRequest(h,pending)!=HAL_OK) {
            recovery_failed(bus,now); return false;
        }
        r->deadline_ms=now+10u;
        recovery_state(bus,SRNOS_FDCAN_RECOVERY_ABORT);
    }
    if(r->state==SRNOS_FDCAN_RECOVERY_ABORT) {
        if(h->Instance->TXBRP) {
            if(reached(now,r->deadline_ms)) recovery_failed(bus,now);
            return false;
        }
        if(HAL_FDCAN_Stop(h)!=HAL_OK || !_activate_notifications(h) || HAL_FDCAN_Start(h)!=HAL_OK) {
            recovery_failed(bus,now); return false;
        }
        r->deadline_ms=now+100u;
        recovery_state(bus,SRNOS_FDCAN_RECOVERY_VERIFY);
    }
    if(r->state==SRNOS_FDCAN_RECOVERY_VERIFY) {
        if(!(h->Instance->PSR & FDCAN_PSR_BO) && !(h->Instance->CCCR & FDCAN_CCCR_INIT)) {
            ++SRNOS_FDCANDiagnostics[bus-1u].recovery_completed;
            recovery_state(bus,SRNOS_FDCAN_RECOVERY_HEALTHY);
            return true;
        }
        if(reached(now,r->deadline_ms)) recovery_failed(bus,now);
    }
    return false;
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
    const SRNOS_CANControllerConfig *config = SRNOS_CANControllerConfigOf(bus);
    if (!h || !config || !tx_hdr || !SRNOS_FDCANIsReady(bus) || tx_hdr->DataLength > 15u ||
        tx_hdr->TxFrameType != FDCAN_DATA_FRAME || tx_hdr->ErrorStateIndicator != FDCAN_ESI_ACTIVE)
        return false;
    const uint8_t length = SRNOS_CANFDDlcToLength((uint8_t)tx_hdr->DataLength);
    const bool fd = config->mode == SRNOS_CAN_MODE_FD;
    if (length == 0xffu || (!payload && length != 0u) ||
        tx_hdr->FDFormat != (fd ? FDCAN_FD_CAN : FDCAN_CLASSIC_CAN) ||
        tx_hdr->BitRateSwitch != ((fd && config->brs) ? FDCAN_BRS_ON : FDCAN_BRS_OFF) ||
        (!fd && length > 8u) ||
        (tx_hdr->IdType != FDCAN_STANDARD_ID && tx_hdr->IdType != FDCAN_EXTENDED_ID) ||
        tx_hdr->Identifier > (tx_hdr->IdType == FDCAN_EXTENDED_ID ? 0x1fffffffu : 0x7ffu)) return false;

    /* Quick check: if no free element, return immediately */
    if (HAL_FDCAN_GetTxFifoFreeLevel(h) == 0) {
        SRNOS_FDCANDiagnostics[bus-1].tx_busy_cnt++;
        return false;
    }
    /* HAL writes complete words, including the final partial word of lengths
     * 1..3 and 5..7. Never read past the caller's exact payload extent. */
    uint8_t staged[64];
    if (length) memcpy(staged, payload, length);
    const uint8_t padded = (uint8_t)((length + 3u) & ~3u);
    if (padded > length) memset(staged + length, 0, padded - length);
    if (HAL_FDCAN_AddMessageToTxFifoQ(h, (FDCAN_TxHeaderTypeDef*)tx_hdr, staged) == HAL_OK) {
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
    uint16_t r = __atomic_load_n(&ring->r,__ATOMIC_RELAXED);
    uint16_t w = __atomic_load_n(&ring->w,__ATOMIC_ACQUIRE);

    while ((r != w) && (n < max_count)) {
        out[n] = ring->buf[r];
        r = (uint16_t)((r + 1u) % SRNOS_FDCAN_RX_RING_SIZE);
        n++;
    }
    __atomic_store_n(&ring->r,r,__ATOMIC_RELEASE);
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

    /* Snapshot the current hardware batch. Newly arriving frames re-pend the
     * IRQ; continuous traffic cannot extend this ISR indefinitely. */
    uint32_t available=HAL_FDCAN_GetRxFifoFillLevel(h,fifo);
    bool published=false;
    while(available--) {
        uint16_t w=__atomic_load_n(&ring->w,__ATOMIC_RELAXED);
        uint16_t next_w=(uint16_t)((w+1u)%SRNOS_FDCAN_RX_RING_SIZE);
        bool full=next_w==__atomic_load_n(&ring->r,__ATOMIC_ACQUIRE);
        /* Drop new frames when full. The producer never modifies the reader
         * index or a slot that the reader might still be copying. */
        SRNOS_FDCANRxFrame discard;
        SRNOS_FDCANRxFrame *dst=full?&discard:&ring->buf[w];
        if(HAL_FDCAN_GetRxMessage(h,fifo,&dst->hdr,dst->data)!=HAL_OK) {
            ++SRNOS_FDCANDiagnostics[bus-1u].rx_error_cnt;
            break;
        }
        ++SRNOS_FDCANDiagnostics[bus-1u].rx_isr_cnt;
        if(full) ++SRNOS_FDCANDiagnostics[bus-1u].rx_overrun_cnt;
        else {
            dst->ts_us=_now_us();
            __atomic_store_n(&ring->w,next_w,__ATOMIC_RELEASE);
            published=true;
        }
    }

    /* Light hook for upper layer (e.g., task notify) */
    if(published) SRNOS_FDCANOnRxISR(bus);
}

/* ==== HAL callbacks (ISR context) ==== */

static uint8_t _bus_of(FDCAN_HandleTypeDef* h) {
    if (h == _handle(1)) return 1;
    if (h == _handle(2)) return 2;
    if (h == _handle(3)) return 3;
    return 0;
}

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *h, uint32_t RxFifo0ITs) {
    uint8_t bus = _bus_of(h);
    if(bus && (RxFifo0ITs & FDCAN_IT_RX_FIFO0_MESSAGE_LOST)) ++SRNOS_FDCANDiagnostics[bus-1u].rx_hw_lost_cnt;
    if (bus) _rx_from_fifo(h, bus, FDCAN_RX_FIFO0);
}

#if SRNOS_FDCAN_ENABLE_RXFIFO1
void HAL_FDCAN_RxFifo1Callback(FDCAN_HandleTypeDef *h, uint32_t RxFifo1ITs) {
    uint8_t bus = _bus_of(h);
    if(bus && (RxFifo1ITs & FDCAN_IT_RX_FIFO1_MESSAGE_LOST)) ++SRNOS_FDCANDiagnostics[bus-1u].rx_hw_lost_cnt;
    if (bus) _rx_from_fifo(h, bus, FDCAN_RX_FIFO1);
}
#endif

void HAL_FDCAN_TxBufferCompleteCallback(FDCAN_HandleTypeDef *h, uint32_t BufferIndexes) {
    uint8_t bus = _bus_of(h);
    if(!bus || !BufferIndexes) return;
    while(BufferIndexes) { BufferIndexes&=BufferIndexes-1u; SRNOS_FDCANOnTxEvent(bus); }
    SRNOS_FDCANOnTxReady(bus);
}

void HAL_FDCAN_TxEventFifoCallback(FDCAN_HandleTypeDef *h, uint32_t TxEventFifoITs) {
    (void)TxEventFifoITs;
    uint8_t bus = _bus_of(h);
    if (!bus) return;

    uint32_t remaining=h->Instance->TXEFS & FDCAN_TXEFS_EFFL_Msk;
    bool completed=false;
    while (remaining--) {
        FDCAN_TxEventFifoTypeDef event;
        if (HAL_FDCAN_GetTxEvent(h, &event) != HAL_OK) {
            break;
        }
        SRNOS_FDCANOnTxEvent(bus);
        completed=true;
    }
    if(completed) SRNOS_FDCANOnTxReady(bus);
}

static void note_busoff(FDCAN_HandleTypeDef *h,uint8_t bus)
{
    if(h->Instance->PSR & FDCAN_PSR_BO) {
        if(!__atomic_exchange_n(&s_recovery[bus-1u].pending,1u,__ATOMIC_ACQ_REL))
            ++SRNOS_FDCANDiagnostics[bus-1u].bus_off_cnt;
    }
    SRNOS_FDCANOnBusoff(bus); /* Also wakes verification when BO clears. */
}
void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *h,uint32_t status)
{
    uint8_t bus=_bus_of(h);
    if(!bus) return;
    SRNOS_FDCANDiagnostics[bus-1u].last_err_code=(h->Instance->PSR & FDCAN_PSR_LEC_Msk)>>FDCAN_PSR_LEC_Pos;
    if(status & FDCAN_IT_BUS_OFF) note_busoff(h,bus);
}
void HAL_FDCAN_ErrorCallback(FDCAN_HandleTypeDef *h)
{
    uint8_t bus=_bus_of(h);
    if(!bus) return;
    SRNOS_FDCANDiagnostics[bus-1u].last_err_code=(h->Instance->PSR & FDCAN_PSR_LEC_Msk)>>FDCAN_PSR_LEC_Pos;
    /* Generic errors can stay latched across unrelated IRQs. Only the BO
     * status-transition callback counts/re-arms a Bus-Off episode. */
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

bool SRNOS_FDCANTxPush(uint8_t bus, uint16_t std_id, const uint8_t* data, uint8_t len)
{
    if (!SRNOS_CANControllerIsClassic(bus) || len > 8u || (!data && len)) return false;

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

    return SRNOS_FDCANTryTx(bus, &tx, data);
}

bool SRNOS_FDCANTxPushExt(uint8_t bus, uint32_t ext_id, const uint8_t* data, uint8_t len)
{
    if (!SRNOS_CANControllerIsClassic(bus) || len > 8u || ext_id > 0x1fffffffu || (!data && len)) return false;

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

    return SRNOS_FDCANTryTx(bus, &tx, data);
}

bool SRNOS_FDCANTxPushFD(uint8_t bus, const SRNOS_CANFDFrame *frame)
{
    if (!frame || !SRNOS_CANControllerIsFD(bus) ||
        (frame->flags & ~(SRNOS_CANFD_FRAME_FLAG_EXT | SRNOS_CANFD_FRAME_FLAG_BRS))) return false;
    const uint8_t dlc = SRNOS_CANFDLengthToDlc(frame->length);
    if (dlc == 0xffu) return false;
    FDCAN_TxHeaderTypeDef tx = {0};
    tx.Identifier = frame->id;
    tx.IdType = (frame->flags & SRNOS_CANFD_FRAME_FLAG_EXT) ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
    tx.TxFrameType = FDCAN_DATA_FRAME;
    tx.DataLength = dlc;
    tx.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    tx.BitRateSwitch = (frame->flags & SRNOS_CANFD_FRAME_FLAG_BRS) ? FDCAN_BRS_ON : FDCAN_BRS_OFF;
    tx.FDFormat = FDCAN_FD_CAN;
    tx.TxEventFifoControl = FDCAN_STORE_TX_EVENTS;
    return SRNOS_FDCANTryTx(bus, &tx, frame->data);
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

void __attribute__((weak)) SRNOS_FDCANOnTxReady(uint8_t bus) { (void)bus; }
