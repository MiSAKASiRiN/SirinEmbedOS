#include "Services/Logging/debug_port_internal.h"

#include "Services/Logging/log_internal.h"
#include "usbd_cdc_acm_if.h"

#include <stdbool.h>

extern USBD_HandleTypeDef SRNOS_USBDevice;

#ifndef SRNOS_DEBUG_PORT_RX_RING_SIZE
#define SRNOS_DEBUG_PORT_RX_RING_SIZE 2048u
#endif
#ifndef SRNOS_DEBUG_PORT_CDC_CHANNEL
#define SRNOS_DEBUG_PORT_CDC_CHANNEL 1u
#endif

_Static_assert(SRNOS_DEBUG_PORT_CDC_CHANNEL < NUMBER_OF_CDC,
               "SRN debug-port CDC channel is not available");

enum {
    SRNOS_DEBUG_PORT_STATUS_OK = 0u,
    SRNOS_DEBUG_PORT_STATUS_TX_ERROR = 4u,
    SRNOS_DEBUG_PORT_STATUS_RX_OK = 10u,
    SRNOS_DEBUG_PORT_STATUS_RX_OVERFLOW = 11u,
    SRNOS_DEBUG_PORT_STATUS_USB_NOT_CONFIGURED = 20u,
    SRNOS_DEBUG_PORT_STATUS_USB_BUSY = 21u,
};

typedef struct {
    uint8_t *buf;
    size_t cap;
    volatile size_t head;
    volatile size_t tail;
} byte_ring_t;

static uint8_t s_rx_ring_buf[SRNOS_DEBUG_PORT_RX_RING_SIZE];
static byte_ring_t s_rx_ring = {
    s_rx_ring_buf,
    SRNOS_DEBUG_PORT_RX_RING_SIZE,
    0u,
    0u,
};

static volatile uint32_t s_session_generation;
static volatile bool s_tx_busy;
static volatile bool s_tx_complete_pending;
static volatile bool s_tx_complete_success;
static volatile uint32_t s_tx_len;
static volatile bool s_rx_enabled;
static TaskHandle_t s_service_task;
static TaskHandle_t s_rx_notify_task;
static uint32_t s_rx_notify_bits;
static uint8_t s_initialized;
static volatile uint8_t s_usb_dtr;

static bool usb_is_configured(void)
{
    return (SRNOS_USBDevice.dev_state == USBD_STATE_CONFIGURED) &&
           (s_usb_dtr != 0u);
}

static size_t ring_count_unlocked(const byte_ring_t *ring)
{
    const size_t head = ring->head;
    const size_t tail = ring->tail;
    return (head >= tail) ? (head - tail) : (ring->cap - (tail - head));
}

static size_t ring_space_unlocked(const byte_ring_t *ring)
{
    return ring->cap - 1u - ring_count_unlocked(ring);
}

static size_t ring_write_unlocked(byte_ring_t *ring,
                                  const uint8_t *data,
                                  size_t len)
{
    const size_t available = ring_space_unlocked(ring);
    if (len > available) {
        len = available;
    }
    for (size_t i = 0u; i < len; ++i) {
        ring->buf[ring->head] = data[i];
        ring->head = (ring->head + 1u) % ring->cap;
    }
    return len;
}

static void sync_rx_pending(void)
{
    SRNOS_DebugLog.usb_rx_pending =
        (uint32_t)ring_count_unlocked(&s_rx_ring);
}

static void debug_touch(uint32_t status)
{
    SRNOS_DbgEnd(&SRNOS_DebugLog.h, HAL_GetTick(), status);
}

static void notify_service_from_isr(BaseType_t *higher_priority_woken)
{
    if (s_service_task != NULL) {
        vTaskNotifyGiveFromISR(s_service_task, higher_priority_woken);
    }
}

static void notify_service_from_task(void)
{
    if (s_service_task != NULL) {
        xTaskNotifyGive(s_service_task);
    }
}

void SRNOS_DebugPortInit(void)
{
    if (s_initialized != 0u) {
        return;
    }
    SRNOS_LogInit();
    taskENTER_CRITICAL();
    if (s_initialized != 0u) {
        taskEXIT_CRITICAL();
        return;
    }
    s_rx_ring.head = 0u;
    s_rx_ring.tail = 0u;
    s_tx_busy = false;
    s_tx_complete_pending = false;
    s_tx_complete_success = false;
    s_tx_len = 0u;
    s_rx_enabled = false;
    s_service_task = NULL;
    s_rx_notify_task = NULL;
    s_rx_notify_bits = 0u;
    s_usb_dtr = 0u;
    SRNOS_DebugLog.rx_enabled = 0u;
    SRNOS_DebugLog.usb_configured = usb_is_configured() ? 1u : 0u;
    SRNOS_DebugLog.tx_high_prio_pending = 0u;
    SRNOS_DebugLog.tx_normal_pending = 0u;
    s_initialized = 1u;
    debug_touch(SRNOS_DEBUG_PORT_STATUS_OK);
    taskEXIT_CRITICAL();
}

void SRNOS_DebugPortRegisterTxServiceTask(TaskHandle_t task)
{
    SRNOS_DebugPortInit();
    s_service_task = task;
}

bool SRNOS_DebugPortIsConfigured(void)
{
    SRNOS_DebugPortInit();
    const bool configured = usb_is_configured();
    SRNOS_DebugLog.usb_configured = configured ? 1u : 0u;
    return configured;
}

bool SRNOS_DebugPortTxBusy(void)
{
    return s_tx_busy;
}

uint32_t SRNOS_DebugPortGetSessionGeneration(void) { return s_session_generation; }

uint32_t SRNOS_DebugPortGetRxOverflowBytes(void)
{
    return SRNOS_DebugLog.usb_rx_overflow;
}

SRNOS_DebugPortWriteStatus SRNOS_DebugPortTryTransmit(const uint8_t *data,
                                                        size_t len)
{
    if ((data == NULL) || (len == 0u)) {
        return SRNOS_DEBUG_PORT_WRITE_INVALID;
    }
    if (len > UINT16_MAX) {
        return SRNOS_DEBUG_PORT_WRITE_TOO_LONG;
    }
    SRNOS_DebugPortInit();
    if (!SRNOS_DebugPortIsConfigured()) {
        SRNOS_DebugLog.usb_tx_drop += (uint32_t)len;
        debug_touch(SRNOS_DEBUG_PORT_STATUS_USB_NOT_CONFIGURED);
        return SRNOS_DEBUG_PORT_WRITE_NOT_CONFIGURED;
    }

    taskENTER_CRITICAL();
    if (s_tx_busy) {
        taskEXIT_CRITICAL();
        SRNOS_DebugLog.usb_tx_busy++;
        return SRNOS_DEBUG_PORT_WRITE_FULL;
    }
    s_tx_busy = true;
    s_tx_len = (uint32_t)len;
    taskEXIT_CRITICAL();

    const uint8_t usb_status = SRNOS_USBCdcTransmit(SRNOS_DEBUG_PORT_CDC_CHANNEL,
                                            (uint8_t *)data,
                                            (uint16_t)len);
    if (usb_status == USBD_OK) {
        return SRNOS_DEBUG_PORT_WRITE_OK;
    }

    taskENTER_CRITICAL();
    s_tx_busy = false;
    s_tx_len = 0u;
    taskEXIT_CRITICAL();
    if (usb_status == USBD_BUSY) {
        SRNOS_DebugLog.usb_tx_busy++;
        debug_touch(SRNOS_DEBUG_PORT_STATUS_USB_BUSY);
        return SRNOS_DEBUG_PORT_WRITE_FULL;
    }
    SRNOS_DebugLog.usb_tx_errors++;
    debug_touch(SRNOS_DEBUG_PORT_STATUS_TX_ERROR);
    return SRNOS_DEBUG_PORT_WRITE_NOT_CONFIGURED;
}

bool SRNOS_DebugPortTakeTxComplete(bool *success)
{
    if (success == NULL) {
        return false;
    }
    taskENTER_CRITICAL();
    const bool pending = s_tx_complete_pending;
    if (pending) {
        *success = s_tx_complete_success;
        s_tx_complete_pending = false;
    }
    taskEXIT_CRITICAL();
    return pending;
}

SRNOS_DebugPortRxStatus SRNOS_DebugPortStartRx(TaskHandle_t notify_task,
                                                 uint32_t notify_bits)
{
    SRNOS_DebugPortInit();
    taskENTER_CRITICAL();
    s_rx_ring.head = 0u;
    s_rx_ring.tail = 0u;
    s_rx_notify_task = notify_task;
    s_rx_notify_bits = notify_bits;
    s_rx_enabled = true;
    SRNOS_DebugLog.rx_enabled = 1u;
    sync_rx_pending();
    taskEXIT_CRITICAL();
    return SRNOS_DEBUG_PORT_RX_OK;
}

void SRNOS_DebugPortStopRx(void)
{
    taskENTER_CRITICAL();
    s_rx_enabled = false;
    s_rx_notify_task = NULL;
    s_rx_notify_bits = 0u;
    SRNOS_DebugLog.rx_enabled = 0u;
    sync_rx_pending();
    taskEXIT_CRITICAL();
}

size_t SRNOS_DebugPortRxAvailable(void)
{
    taskENTER_CRITICAL();
    const size_t count = ring_count_unlocked(&s_rx_ring);
    taskEXIT_CRITICAL();
    return count;
}

size_t SRNOS_DebugPortRead(uint8_t *out, size_t max_len)
{
    if ((out == NULL) || (max_len == 0u)) {
        return 0u;
    }
    size_t count = 0u;
    taskENTER_CRITICAL();
    while ((count < max_len) && (ring_count_unlocked(&s_rx_ring) != 0u)) {
        out[count++] = s_rx_ring.buf[s_rx_ring.tail];
        s_rx_ring.tail = (s_rx_ring.tail + 1u) % s_rx_ring.cap;
    }
    sync_rx_pending();
    taskEXIT_CRITICAL();
    return count;
}

void SRNOS_DebugPortOnUsbReceive(const uint8_t *data, uint32_t len)
{
    if ((s_initialized == 0u) || (data == NULL) || (len == 0u)) {
        return;
    }
    BaseType_t higher_priority_woken = pdFALSE;
    size_t dropped = 0u;
    bool should_notify = false;
    const UBaseType_t saved = taskENTER_CRITICAL_FROM_ISR();
    if (!s_rx_enabled) {
        dropped = len;
    } else {
        const bool was_empty = ring_count_unlocked(&s_rx_ring) == 0u;
        const size_t written = ring_write_unlocked(&s_rx_ring, data, len);
        dropped = (size_t)len - written;
        should_notify = was_empty && (written != 0u);
        SRNOS_DebugLog.rx_bytes += (uint32_t)written;
        SRNOS_DebugLog.rx_events++;
        SRNOS_DebugLog.last_rx_size = len;
        SRNOS_DebugLog.usb_rx_bytes += (uint32_t)written;
        sync_rx_pending();
    }
    if (dropped != 0u) {
        SRNOS_DebugLog.rx_overflow_bytes += (uint32_t)dropped;
        SRNOS_DebugLog.usb_rx_overflow += (uint32_t)dropped;
        SRNOS_DebugLog.usb_rx_drop += (uint32_t)dropped;
    }
    taskEXIT_CRITICAL_FROM_ISR(saved);

    if (should_notify && (s_rx_notify_task != NULL)) {
        if (s_rx_notify_bits != 0u) {
            (void)xTaskNotifyFromISR(s_rx_notify_task, s_rx_notify_bits,
                                     eSetBits, &higher_priority_woken);
        } else {
            vTaskNotifyGiveFromISR(s_rx_notify_task, &higher_priority_woken);
        }
    }
    debug_touch((dropped == 0u) ? SRNOS_DEBUG_PORT_STATUS_RX_OK :
                                  SRNOS_DEBUG_PORT_STATUS_RX_OVERFLOW);
    portYIELD_FROM_ISR(higher_priority_woken);
}

void SRNOS_DebugPortOnUsbTransmitComplete(void)
{
    if (s_initialized == 0u) {
        return;
    }
    BaseType_t higher_priority_woken = pdFALSE;
    const UBaseType_t saved = taskENTER_CRITICAL_FROM_ISR();
    if (s_tx_busy) {
        SRNOS_DebugLog.usb_tx_bytes += s_tx_len;
        s_tx_len = 0u;
        s_tx_busy = false;
        s_tx_complete_success = true;
        s_tx_complete_pending = true;
    }
    taskEXIT_CRITICAL_FROM_ISR(saved);
    notify_service_from_isr(&higher_priority_woken);
    debug_touch(SRNOS_DEBUG_PORT_STATUS_OK);
    portYIELD_FROM_ISR(higher_priority_woken);
}

void SRNOS_DebugPortOnUsbDisconnect(void)
{
    ++s_session_generation;
    if (s_initialized == 0u) {
        return;
    }
    if (xPortIsInsideInterrupt() == pdTRUE) {
        BaseType_t higher_priority_woken = pdFALSE;
        const UBaseType_t saved = taskENTER_CRITICAL_FROM_ISR();
        s_usb_dtr = 0u;
        s_rx_ring.head = 0u;
        s_rx_ring.tail = 0u;
        sync_rx_pending();
        if (s_tx_busy) {
            s_tx_len = 0u;
            s_tx_busy = false;
            s_tx_complete_success = false;
            s_tx_complete_pending = true;
        }
        taskEXIT_CRITICAL_FROM_ISR(saved);
        notify_service_from_isr(&higher_priority_woken);
        portYIELD_FROM_ISR(higher_priority_woken);
    } else {
        taskENTER_CRITICAL();
        s_usb_dtr = 0u;
        s_rx_ring.head = 0u;
        s_rx_ring.tail = 0u;
        sync_rx_pending();
        if (s_tx_busy) {
            s_tx_len = 0u;
            s_tx_busy = false;
            s_tx_complete_success = false;
            s_tx_complete_pending = true;
        }
        taskEXIT_CRITICAL();
        notify_service_from_task();
    }
}

void SRNOS_DebugPortOnUsbControlLineState(uint16_t state)
{
    const uint8_t was_asserted = s_usb_dtr;
    s_usb_dtr = ((state & 0x0001u) != 0u) ? 1u : 0u;
    if ((was_asserted != 0u) && (s_usb_dtr == 0u)) {
        /* The class may still reference the in-flight buffer. Only DeInit
         * (after CloseEP) or actual TX completion can release it. */
        ++s_session_generation;
        s_rx_ring.head=s_rx_ring.tail=0;
        if(xPortIsInsideInterrupt()==pdTRUE) {
            BaseType_t wake=pdFALSE; notify_service_from_isr(&wake); portYIELD_FROM_ISR(wake);
        } else notify_service_from_task();
    }
}
