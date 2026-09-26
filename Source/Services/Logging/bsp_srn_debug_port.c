#include "SirinEmbedOS/Services/Logging/bsp_srn_debug_port.h"

#include "SirinEmbedOS/Services/Logging/bsp_srn_log.h"
#include "usbd_cdc_acm_if.h"

#include <stdbool.h>

extern USBD_HandleTypeDef hUsbDevice;

#ifndef SRN_DEBUG_PORT_RX_RING_SIZE
#define SRN_DEBUG_PORT_RX_RING_SIZE 2048u
#endif
#ifndef SRN_DEBUG_PORT_CDC_CHANNEL
#define SRN_DEBUG_PORT_CDC_CHANNEL 1u
#endif

_Static_assert(SRN_DEBUG_PORT_CDC_CHANNEL < NUMBER_OF_CDC,
               "SRN debug-port CDC channel is not available");

enum {
    SRN_DEBUG_PORT_STATUS_OK = 0u,
    SRN_DEBUG_PORT_STATUS_TX_ERROR = 4u,
    SRN_DEBUG_PORT_STATUS_RX_OK = 10u,
    SRN_DEBUG_PORT_STATUS_RX_OVERFLOW = 11u,
    SRN_DEBUG_PORT_STATUS_USB_NOT_CONFIGURED = 20u,
    SRN_DEBUG_PORT_STATUS_USB_BUSY = 21u,
};

typedef struct {
    uint8_t *buf;
    size_t cap;
    volatile size_t head;
    volatile size_t tail;
} byte_ring_t;

static uint8_t s_rx_ring_buf[SRN_DEBUG_PORT_RX_RING_SIZE];
static byte_ring_t s_rx_ring = {
    s_rx_ring_buf,
    SRN_DEBUG_PORT_RX_RING_SIZE,
    0u,
    0u,
};

static volatile bool s_tx_busy;
static volatile bool s_tx_complete_pending;
static volatile bool s_tx_complete_success;
static volatile uint32_t s_tx_len;
static volatile bool s_rx_enabled;
static TaskHandle_t s_service_task;
static TaskHandle_t s_rx_notify_task;
static uint32_t s_rx_notify_bits;
static srn_debug_port_write_sink_fn s_write_sink;
static void *s_write_sink_user;
static uint8_t s_initialized;
static volatile uint8_t s_usb_dtr;

static bool usb_is_configured(void)
{
    return (hUsbDevice.dev_state == USBD_STATE_CONFIGURED) &&
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
    g_omx_dbg_srn_log.usb_rx_pending =
        (uint32_t)ring_count_unlocked(&s_rx_ring);
}

static void debug_touch(uint32_t status)
{
    omx_dbg_end(&g_omx_dbg_srn_log.h, HAL_GetTick(), status);
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

void SRN_DebugPort_Init(void)
{
    if (s_initialized != 0u) {
        return;
    }
    SRN_Log_Init();
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
    s_write_sink = NULL;
    s_write_sink_user = NULL;
    s_usb_dtr = 0u;
    g_omx_dbg_srn_log.rx_enabled = 0u;
    g_omx_dbg_srn_log.usb_configured = usb_is_configured() ? 1u : 0u;
    g_omx_dbg_srn_log.tx_high_prio_pending = 0u;
    g_omx_dbg_srn_log.tx_normal_pending = 0u;
    s_initialized = 1u;
    debug_touch(SRN_DEBUG_PORT_STATUS_OK);
    taskEXIT_CRITICAL();
}

void SRN_DebugPort_RegisterTxServiceTask(TaskHandle_t task)
{
    SRN_DebugPort_Init();
    s_service_task = task;
}

void SRN_DebugPort_RegisterWriteSink(srn_debug_port_write_sink_fn sink,
                                    void *user)
{
    SRN_DebugPort_Init();
    taskENTER_CRITICAL();
    s_write_sink = sink;
    s_write_sink_user = user;
    taskEXIT_CRITICAL();
}

void SRN_DebugPort_ServiceTxOnce(void)
{
    /* TX scheduling is owned by HostComm in protocol V2. */
}

bool SRN_DebugPort_IsConfigured(void)
{
    SRN_DebugPort_Init();
    const bool configured = usb_is_configured();
    g_omx_dbg_srn_log.usb_configured = configured ? 1u : 0u;
    return configured;
}

bool SRN_DebugPort_TxBusy(void)
{
    return s_tx_busy;
}

uint32_t SRN_DebugPort_GetRxOverflowBytes(void)
{
    return g_omx_dbg_srn_log.usb_rx_overflow;
}

srn_debug_port_write_status_t SRN_DebugPort_TryTransmit(const uint8_t *data,
                                                        size_t len)
{
    if ((data == NULL) || (len == 0u)) {
        return SRN_DEBUG_PORT_WRITE_INVALID;
    }
    if (len > UINT16_MAX) {
        return SRN_DEBUG_PORT_WRITE_TOO_LONG;
    }
    SRN_DebugPort_Init();
    if (!SRN_DebugPort_IsConfigured()) {
        g_omx_dbg_srn_log.usb_tx_drop += (uint32_t)len;
        debug_touch(SRN_DEBUG_PORT_STATUS_USB_NOT_CONFIGURED);
        return SRN_DEBUG_PORT_WRITE_NOT_CONFIGURED;
    }

    taskENTER_CRITICAL();
    if (s_tx_busy) {
        taskEXIT_CRITICAL();
        g_omx_dbg_srn_log.usb_tx_busy++;
        return SRN_DEBUG_PORT_WRITE_FULL;
    }
    s_tx_busy = true;
    s_tx_len = (uint32_t)len;
    taskEXIT_CRITICAL();

    const uint8_t usb_status = CDC_Transmit(SRN_DEBUG_PORT_CDC_CHANNEL,
                                            (uint8_t *)data,
                                            (uint16_t)len);
    if (usb_status == USBD_OK) {
        return SRN_DEBUG_PORT_WRITE_OK;
    }

    taskENTER_CRITICAL();
    s_tx_busy = false;
    s_tx_len = 0u;
    taskEXIT_CRITICAL();
    if (usb_status == USBD_BUSY) {
        g_omx_dbg_srn_log.usb_tx_busy++;
        debug_touch(SRN_DEBUG_PORT_STATUS_USB_BUSY);
        return SRN_DEBUG_PORT_WRITE_FULL;
    }
    g_omx_dbg_srn_log.usb_tx_errors++;
    debug_touch(SRN_DEBUG_PORT_STATUS_TX_ERROR);
    return SRN_DEBUG_PORT_WRITE_NOT_CONFIGURED;
}

bool SRN_DebugPort_TakeTxComplete(bool *success)
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

static srn_debug_port_write_status_t write_to_sink(const void *data,
                                                   size_t len,
                                                   bool high_priority)
{
    if ((data == NULL) || (len == 0u)) {
        return SRN_DEBUG_PORT_WRITE_INVALID;
    }
    SRN_DebugPort_Init();
    if ((s_write_sink == NULL) || (xPortIsInsideInterrupt() == pdTRUE)) {
        g_omx_dbg_srn_log.usb_tx_drop += (uint32_t)len;
        return SRN_DEBUG_PORT_WRITE_NOT_CONFIGURED;
    }
    const srn_debug_port_write_status_t status =
        s_write_sink(s_write_sink_user, data, len, high_priority);
    if (high_priority) {
        g_omx_dbg_srn_log.tx_high_prio_last_len = (uint32_t)len;
        if (status == SRN_DEBUG_PORT_WRITE_OK) {
            g_omx_dbg_srn_log.tx_high_prio_writes++;
            g_omx_dbg_srn_log.tx_high_prio_bytes += (uint32_t)len;
        } else {
            g_omx_dbg_srn_log.tx_high_prio_full++;
        }
    }
    if (status != SRN_DEBUG_PORT_WRITE_OK) {
        g_omx_dbg_srn_log.usb_tx_drop += (uint32_t)len;
    }
    if (status == SRN_DEBUG_PORT_WRITE_OK) {
        notify_service_from_task();
    }
    return status;
}

srn_debug_port_write_status_t SRN_DebugPort_WriteExact(const void *data,
                                                       size_t len)
{
    return write_to_sink(data, len, false);
}

srn_debug_port_write_status_t SRN_DebugPort_WriteHighPrioExact(const void *data,
                                                               size_t len)
{
    return write_to_sink(data, len, true);
}

size_t SRN_DebugPort_Write(const void *data, size_t len, TickType_t timeout)
{
    (void)timeout;
    return (SRN_DebugPort_WriteExact(data, len) == SRN_DEBUG_PORT_WRITE_OK) ? len : 0u;
}

size_t SRN_DebugPort_WriteStr(const char *text, TickType_t timeout)
{
    if (text == NULL) {
        return 0u;
    }
    size_t len = 0u;
    while (text[len] != '\0') {
        ++len;
    }
    return SRN_DebugPort_Write(text, len, timeout);
}

int SRN_DebugPort_PutChar(int ch, TickType_t timeout)
{
    const uint8_t value = (uint8_t)ch;
    return (SRN_DebugPort_Write(&value, 1u, timeout) == 1u) ? ch : -1;
}

srn_debug_port_rx_status_t SRN_DebugPort_StartRx(TaskHandle_t notify_task,
                                                 uint32_t notify_bits)
{
    SRN_DebugPort_Init();
    taskENTER_CRITICAL();
    s_rx_ring.head = 0u;
    s_rx_ring.tail = 0u;
    s_rx_notify_task = notify_task;
    s_rx_notify_bits = notify_bits;
    s_rx_enabled = true;
    g_omx_dbg_srn_log.rx_enabled = 1u;
    sync_rx_pending();
    taskEXIT_CRITICAL();
    return SRN_DEBUG_PORT_RX_OK;
}

void SRN_DebugPort_StopRx(void)
{
    taskENTER_CRITICAL();
    s_rx_enabled = false;
    s_rx_notify_task = NULL;
    s_rx_notify_bits = 0u;
    g_omx_dbg_srn_log.rx_enabled = 0u;
    sync_rx_pending();
    taskEXIT_CRITICAL();
}

size_t SRN_DebugPort_RxAvailable(void)
{
    taskENTER_CRITICAL();
    const size_t count = ring_count_unlocked(&s_rx_ring);
    taskEXIT_CRITICAL();
    return count;
}

size_t SRN_DebugPort_Read(uint8_t *out, size_t max_len)
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

void SRN_DebugPort_OnUsbReceive(const uint8_t *data, uint32_t len)
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
        g_omx_dbg_srn_log.rx_bytes += (uint32_t)written;
        g_omx_dbg_srn_log.rx_events++;
        g_omx_dbg_srn_log.last_rx_size = len;
        g_omx_dbg_srn_log.usb_rx_bytes += (uint32_t)written;
        sync_rx_pending();
    }
    if (dropped != 0u) {
        g_omx_dbg_srn_log.rx_overflow_bytes += (uint32_t)dropped;
        g_omx_dbg_srn_log.usb_rx_overflow += (uint32_t)dropped;
        g_omx_dbg_srn_log.usb_rx_drop += (uint32_t)dropped;
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
    debug_touch((dropped == 0u) ? SRN_DEBUG_PORT_STATUS_RX_OK :
                                  SRN_DEBUG_PORT_STATUS_RX_OVERFLOW);
    portYIELD_FROM_ISR(higher_priority_woken);
}

void SRN_DebugPort_OnUsbTransmitComplete(void)
{
    if (s_initialized == 0u) {
        return;
    }
    BaseType_t higher_priority_woken = pdFALSE;
    const UBaseType_t saved = taskENTER_CRITICAL_FROM_ISR();
    if (s_tx_busy) {
        g_omx_dbg_srn_log.usb_tx_bytes += s_tx_len;
        s_tx_len = 0u;
        s_tx_busy = false;
        s_tx_complete_success = true;
        s_tx_complete_pending = true;
    }
    taskEXIT_CRITICAL_FROM_ISR(saved);
    notify_service_from_isr(&higher_priority_woken);
    debug_touch(SRN_DEBUG_PORT_STATUS_OK);
    portYIELD_FROM_ISR(higher_priority_woken);
}

void SRN_DebugPort_OnUsbDisconnect(void)
{
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

void SRN_DebugPort_OnUsbControlLineState(uint16_t state)
{
    const uint8_t was_asserted = s_usb_dtr;
    s_usb_dtr = ((state & 0x0001u) != 0u) ? 1u : 0u;
    if ((was_asserted != 0u) && (s_usb_dtr == 0u)) {
        SRN_DebugPort_OnUsbDisconnect();
    }
}
