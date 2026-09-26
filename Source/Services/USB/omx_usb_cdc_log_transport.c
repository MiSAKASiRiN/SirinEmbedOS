#include "omx_usb_cdc_log_transport.h"

#include "SirinEmbedOS/Core/C/Bounds.h"
#include "SirinEmbedOS/Core/C/Memory.h"
#include "FreeRTOS.h"
#include "SirinEmbedOS/Services/Logging/bsp_srn_log.h"
#include "omx_boot_diag.h"
#include "SirinEmbedOS/Diagnostics/Watch/omx_debug_watch.h"
#include "omx_usb_cdc_channels.h"
#include "task.h"
#include <usbd_cdc_acm_if.h>

#define OMX_USB_CDC_LOG_QUEUE_CAPACITY 16u
#define OMX_USB_CDC_LOG_SLOT_SIZE      384u

typedef struct {
    uint16_t length;
    uint8_t data[OMX_USB_CDC_LOG_SLOT_SIZE];
} omx_usb_cdc_log_slot_t;

typedef enum {
    OMX_USB_CDC_LOG_TX_NONE = 0,
    OMX_USB_CDC_LOG_TX_REPLAY,
    OMX_USB_CDC_LOG_TX_LIVE,
} omx_usb_cdc_log_tx_kind_t;

extern USBD_HandleTypeDef hUsbDevice;

static omx_usb_cdc_log_slot_t s_queue[OMX_USB_CDC_LOG_QUEUE_CAPACITY];
static uint8_t s_live_tx_buffer[OMX_USB_CDC_LOG_SLOT_SIZE];
static volatile size_t s_queue_head;
static volatile size_t s_queue_count;
static volatile uint32_t s_session_generation;
static volatile uint32_t s_tx_generation;
static uint32_t s_service_generation;
static volatile uint16_t s_tx_length;
static volatile uint8_t s_dtr;
static volatile uint8_t s_tx_active;
static volatile uint8_t s_tx_complete_pending;
static volatile uint8_t s_tx_kind;
static volatile uint8_t s_initialized;

_Static_assert(OMX_USB_CDC_LOG_SLOT_SIZE >= SRN_LOG_LINE_MAX,
               "CDC0 text slot must hold one complete formatted log line");

static volatile omx_dbg_usb_cdc_t *log_watch(void)
{
    return &g_omx_dbg_usb_cdc[OMX_USB_CDC_LOG_CHANNEL];
}

static bool usb_device_configured(void)
{
    return hUsbDevice.dev_state == USBD_STATE_CONFIGURED;
}

static void sync_watch(void)
{
    volatile omx_dbg_usb_cdc_t *watch = log_watch();
    watch->configured = usb_device_configured() ? 1u : 0u;
    watch->dtr = s_dtr;
    watch->tx_active = s_tx_active;
    watch->queue_depth = (uint32_t)s_queue_count;
}

static void clear_session_unlocked(void)
{
    s_queue_head = 0u;
    s_queue_count = 0u;
    s_tx_active = 0u;
    s_tx_complete_pending = 0u;
    s_tx_kind = OMX_USB_CDC_LOG_TX_NONE;
    s_tx_length = 0u;
    s_tx_generation = 0u;
}

void omx_usb_cdc_log_transport_init(void)
{
    omx_usb_cdc_channels_init();
    if (s_initialized != 0u) {
        return;
    }
    taskENTER_CRITICAL();
    if (s_initialized != 0u) {
        taskEXIT_CRITICAL();
        return;
    }
    clear_session_unlocked();
    s_session_generation = 1u;
    s_service_generation = 0u;
    s_dtr = 0u;
    s_initialized = 1u;
    sync_watch();
    taskEXIT_CRITICAL();
}

bool omx_usb_cdc_log_is_connected(void)
{
    omx_usb_cdc_log_transport_init();
    sync_watch();
    return usb_device_configured() && (s_dtr != 0u);
}

bool omx_usb_cdc_log_submit(void *user,
                            const uint8_t *data,
                            size_t length)
{
    (void)user;
    omx_usb_cdc_log_transport_init();
    volatile omx_dbg_usb_cdc_t *watch = log_watch();
    if ((data == NULL) || (length == 0u) ||
        (length > OMX_USB_CDC_LOG_SLOT_SIZE)) {
        watch->tx_drop += (uint32_t)length;
        return false;
    }

    taskENTER_CRITICAL();
    if (!usb_device_configured() || (s_dtr == 0u)) {
        watch->tx_drop += (uint32_t)length;
        sync_watch();
        taskEXIT_CRITICAL();
        return false;
    }

    if (s_queue_count == OMX_USB_CDC_LOG_QUEUE_CAPACITY) {
        watch->tx_drop += s_queue[s_queue_head].length;
        s_queue_head = (s_queue_head + 1u) % OMX_USB_CDC_LOG_QUEUE_CAPACITY;
        --s_queue_count;
    }
    const size_t tail =
        (s_queue_head + s_queue_count) % OMX_USB_CDC_LOG_QUEUE_CAPACITY;
    if (!SRNOS_Memory_Copy(
            SRNOS_Memory_MutableBytes(s_queue[tail].data,
                                   sizeof(s_queue[tail].data)),
            SRNOS_Memory_Bytes(data, length))) {
        watch->tx_drop += (uint32_t)length;
        taskEXIT_CRITICAL();
        return false;
    }
    s_queue[tail].length = (uint16_t)length;
    ++s_queue_count;
    if (s_queue_count > watch->queue_high_water) {
        watch->queue_high_water = (uint32_t)s_queue_count;
    }
    sync_watch();
    taskEXIT_CRITICAL();
    return true;
}

static void process_tx_complete(void)
{
    uint8_t kind = OMX_USB_CDC_LOG_TX_NONE;
    uint16_t length = 0u;
    uint32_t generation = 0u;
    taskENTER_CRITICAL();
    if (s_tx_complete_pending != 0u) {
        kind = s_tx_kind;
        length = s_tx_length;
        generation = s_tx_generation;
        s_tx_complete_pending = 0u;
        s_tx_kind = OMX_USB_CDC_LOG_TX_NONE;
        s_tx_length = 0u;
        s_tx_generation = 0u;
    }
    taskEXIT_CRITICAL();

    if ((kind == OMX_USB_CDC_LOG_TX_REPLAY) &&
        (generation == s_service_generation) &&
        !omx_boot_diag_replay_commit(length)) {
        log_watch()->tx_errors++;
    }
}

static bool begin_transmit(const uint8_t *data,
                           size_t length,
                           omx_usb_cdc_log_tx_kind_t kind,
                           uint32_t generation)
{
    if ((data == NULL) || (length == 0u) || (length > UINT16_MAX)) {
        return false;
    }

    taskENTER_CRITICAL();
    if (!usb_device_configured() || (s_dtr == 0u) ||
        (s_tx_active != 0u) || (s_session_generation != generation)) {
        taskEXIT_CRITICAL();
        return false;
    }
    s_tx_active = 1u;
    s_tx_kind = (uint8_t)kind;
    s_tx_length = (uint16_t)length;
    s_tx_generation = generation;
    sync_watch();
    taskEXIT_CRITICAL();

    const uint8_t status = CDC_Transmit(OMX_USB_CDC_LOG_CHANNEL,
                                        (uint8_t *)data,
                                        (uint16_t)length);
    if (status == USBD_OK) {
        volatile omx_dbg_usb_cdc_t *watch = log_watch();
        watch->tx_events++;
        watch->last_tx_len = (uint32_t)length;
        return true;
    }

    taskENTER_CRITICAL();
    s_tx_active = 0u;
    s_tx_kind = OMX_USB_CDC_LOG_TX_NONE;
    s_tx_length = 0u;
    s_tx_generation = 0u;
    sync_watch();
    taskEXIT_CRITICAL();
    if (status == USBD_BUSY) {
        log_watch()->tx_busy++;
    } else {
        log_watch()->tx_errors++;
    }
    return false;
}

void omx_usb_cdc_log_service(void *user)
{
    (void)user;
    omx_usb_cdc_log_transport_init();

    const uint32_t generation = s_session_generation;
    if (generation != s_service_generation) {
        omx_boot_diag_replay_reset();
        s_service_generation = generation;
        log_watch()->replay_restarts++;
    }

    process_tx_complete();
    if (!omx_usb_cdc_log_is_connected() || (s_tx_active != 0u)) {
        return;
    }

    const uint8_t *replay_data = NULL;
    size_t replay_length = 0u;
    if (omx_boot_diag_replay_peek(&replay_data, &replay_length)) {
        log_watch()->replay_active = 1u;
        (void)begin_transmit(replay_data, replay_length,
                             OMX_USB_CDC_LOG_TX_REPLAY,
                             s_service_generation);
        return;
    }
    log_watch()->replay_active = 0u;

    size_t live_length = 0u;
    taskENTER_CRITICAL();
    if (s_queue_count != 0u) {
        const omx_usb_cdc_log_slot_t *slot = &s_queue[s_queue_head];
        live_length = slot->length;
        if (!SRNOS_Bounds_RangeValid(0u, live_length,
                                  sizeof(s_live_tx_buffer)) ||
            !SRNOS_Memory_Copy(
                SRNOS_Memory_MutableBytes(s_live_tx_buffer,
                                       sizeof(s_live_tx_buffer)),
                SRNOS_Memory_Bytes(slot->data, live_length))) {
            log_watch()->tx_errors++;
            live_length = 0u;
        }
        s_queue_head = (s_queue_head + 1u) % OMX_USB_CDC_LOG_QUEUE_CAPACITY;
        --s_queue_count;
        sync_watch();
    }
    taskEXIT_CRITICAL();

    if (live_length != 0u) {
        if (!begin_transmit(s_live_tx_buffer, live_length,
                            OMX_USB_CDC_LOG_TX_LIVE,
                            s_service_generation)) {
            log_watch()->tx_drop += (uint32_t)live_length;
        }
    }
}

void omx_usb_cdc_log_on_receive(uint32_t length)
{
    omx_usb_cdc_channels_init();
    volatile omx_dbg_usb_cdc_t *watch = log_watch();
    watch->rx_events++;
    watch->rx_bytes += length;
    watch->rx_drop += length;
    watch->last_rx_len = length;
}

void omx_usb_cdc_log_on_transmit_complete(void)
{
    if (s_initialized == 0u) {
        return;
    }
    const UBaseType_t saved = taskENTER_CRITICAL_FROM_ISR();
    volatile omx_dbg_usb_cdc_t *watch = log_watch();
    if ((s_tx_active != 0u) && (s_dtr != 0u)) {
        watch->tx_bytes += s_tx_length;
        watch->tx_complete++;
        s_tx_complete_pending = 1u;
    }
    s_tx_active = 0u;
    sync_watch();
    taskEXIT_CRITICAL_FROM_ISR(saved);
}

static void set_dtr(bool asserted, bool from_isr)
{
    UBaseType_t saved = 0u;
    if (from_isr) {
        saved = taskENTER_CRITICAL_FROM_ISR();
    } else {
        taskENTER_CRITICAL();
    }

    const bool was_asserted = s_dtr != 0u;
    if (asserted != was_asserted) {
        clear_session_unlocked();
        s_dtr = asserted ? 1u : 0u;
        ++s_session_generation;
    }
    sync_watch();

    if (from_isr) {
        taskEXIT_CRITICAL_FROM_ISR(saved);
    } else {
        taskEXIT_CRITICAL();
    }
}

void omx_usb_cdc_log_on_disconnect(void)
{
    if (s_initialized == 0u) {
        return;
    }
    set_dtr(false, xPortIsInsideInterrupt() == pdTRUE);
}

void omx_usb_cdc_log_on_control_line_state(uint16_t state)
{
    if (s_initialized == 0u) {
        return;
    }
    set_dtr((state & 0x0001u) != 0u,
            xPortIsInsideInterrupt() == pdTRUE);
}
