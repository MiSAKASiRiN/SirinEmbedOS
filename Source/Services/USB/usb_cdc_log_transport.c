#include "usb_cdc_log_transport.h"

#include "SirinEmbedOS/Core/C/Bounds.h"
#include "SirinEmbedOS/Core/C/Memory.h"
#include "FreeRTOS.h"
#include "Services/Logging/log_internal.h"
#include "boot_diag.h"
#include "SirinEmbedOS/Diagnostics/Watch/Watch.h"
#include "usb_cdc_channels.h"
#include "task.h"
#include <usbd_cdc_acm_if.h>

#define SRNOS_USB_CDC_LOG_QUEUE_CAPACITY 16u
#define SRNOS_USB_CDC_LOG_SLOT_SIZE      384u

typedef struct {
    uint16_t length;
    uint8_t data[SRNOS_USB_CDC_LOG_SLOT_SIZE];
} SRNOS_USBCdcLogSlot;

typedef enum {
    SRNOS_USB_CDC_LOG_TX_NONE = 0,
    SRNOS_USB_CDC_LOG_TX_REPLAY,
    SRNOS_USB_CDC_LOG_TX_LIVE,
} SRNOS_USBCdcLogTxKind;

extern USBD_HandleTypeDef SRNOS_USBDevice;

static SRNOS_USBCdcLogSlot s_queue[SRNOS_USB_CDC_LOG_QUEUE_CAPACITY];
static uint8_t s_live_tx_buffer[SRNOS_USB_CDC_LOG_SLOT_SIZE];
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

_Static_assert(SRNOS_USB_CDC_LOG_SLOT_SIZE >= SRNOS_LOG_LINE_MAX,
               "CDC0 text slot must hold one complete formatted log line");

static volatile SRNOS_DbgUSBCdc *log_watch(void)
{
    return &SRNOS_DebugDbgUSBCdc[SRNOS_USB_CDC_LOG_CHANNEL];
}

static bool usb_device_configured(void)
{
    return SRNOS_USBDevice.dev_state == USBD_STATE_CONFIGURED;
}

static void sync_watch(void)
{
    volatile SRNOS_DbgUSBCdc *watch = log_watch();
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
    s_tx_kind = SRNOS_USB_CDC_LOG_TX_NONE;
    s_tx_length = 0u;
    s_tx_generation = 0u;
}

void SRNOS_USBCdcLogTransportInit(void)
{
    SRNOS_USBCdcChannelsInit();
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

bool SRNOS_USBCdcLogIsConnected(void)
{
    SRNOS_USBCdcLogTransportInit();
    sync_watch();
    return usb_device_configured() && (s_dtr != 0u);
}

bool SRNOS_USBCdcLogSubmit(void *user,
                            const uint8_t *data,
                            size_t length)
{
    (void)user;
    SRNOS_USBCdcLogTransportInit();
    volatile SRNOS_DbgUSBCdc *watch = log_watch();
    if ((data == NULL) || (length == 0u) ||
        (length > SRNOS_USB_CDC_LOG_SLOT_SIZE)) {
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

    if (s_queue_count == SRNOS_USB_CDC_LOG_QUEUE_CAPACITY) {
        watch->tx_drop += s_queue[s_queue_head].length;
        s_queue_head = (s_queue_head + 1u) % SRNOS_USB_CDC_LOG_QUEUE_CAPACITY;
        --s_queue_count;
    }
    const size_t tail =
        (s_queue_head + s_queue_count) % SRNOS_USB_CDC_LOG_QUEUE_CAPACITY;
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
    uint8_t kind = SRNOS_USB_CDC_LOG_TX_NONE;
    uint16_t length = 0u;
    uint32_t generation = 0u;
    taskENTER_CRITICAL();
    if (s_tx_complete_pending != 0u) {
        kind = s_tx_kind;
        length = s_tx_length;
        generation = s_tx_generation;
        s_tx_complete_pending = 0u;
        s_tx_kind = SRNOS_USB_CDC_LOG_TX_NONE;
        s_tx_length = 0u;
        s_tx_generation = 0u;
    }
    taskEXIT_CRITICAL();

    if ((kind == SRNOS_USB_CDC_LOG_TX_REPLAY) &&
        (generation == s_service_generation) &&
        !SRNOS_BootDiagReplayCommit(length)) {
        log_watch()->tx_errors++;
    }
}

static bool begin_transmit(const uint8_t *data,
                           size_t length,
                           SRNOS_USBCdcLogTxKind kind,
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

    const uint8_t status = SRNOS_USBCdcTransmit(SRNOS_USB_CDC_LOG_CHANNEL,
                                        (uint8_t *)data,
                                        (uint16_t)length);
    if (status == USBD_OK) {
        volatile SRNOS_DbgUSBCdc *watch = log_watch();
        watch->tx_events++;
        watch->last_tx_len = (uint32_t)length;
        return true;
    }

    taskENTER_CRITICAL();
    s_tx_active = 0u;
    s_tx_kind = SRNOS_USB_CDC_LOG_TX_NONE;
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

void SRNOS_USBCdcLogService(void *user)
{
    (void)user;
    SRNOS_USBCdcLogTransportInit();

    const uint32_t generation = s_session_generation;
    if (generation != s_service_generation) {
        SRNOS_BootDiagReplayReset();
        s_service_generation = generation;
        log_watch()->replay_restarts++;
    }

    process_tx_complete();
    if (!SRNOS_USBCdcLogIsConnected() || (s_tx_active != 0u)) {
        return;
    }

    const uint8_t *replay_data = NULL;
    size_t replay_length = 0u;
    if (SRNOS_BootDiagReplayPeek(&replay_data, &replay_length)) {
        log_watch()->replay_active = 1u;
        (void)begin_transmit(replay_data, replay_length,
                             SRNOS_USB_CDC_LOG_TX_REPLAY,
                             s_service_generation);
        return;
    }
    log_watch()->replay_active = 0u;

    size_t live_length = 0u;
    taskENTER_CRITICAL();
    if (s_queue_count != 0u) {
        const SRNOS_USBCdcLogSlot *slot = &s_queue[s_queue_head];
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
        s_queue_head = (s_queue_head + 1u) % SRNOS_USB_CDC_LOG_QUEUE_CAPACITY;
        --s_queue_count;
        sync_watch();
    }
    taskEXIT_CRITICAL();

    if (live_length != 0u) {
        if (!begin_transmit(s_live_tx_buffer, live_length,
                            SRNOS_USB_CDC_LOG_TX_LIVE,
                            s_service_generation)) {
            log_watch()->tx_drop += (uint32_t)live_length;
        }
    }
}

void SRNOS_USBCdcLogOnReceive(uint32_t length)
{
    SRNOS_USBCdcChannelsInit();
    volatile SRNOS_DbgUSBCdc *watch = log_watch();
    watch->rx_events++;
    watch->rx_bytes += length;
    watch->rx_drop += length;
    watch->last_rx_len = length;
}

void SRNOS_USBCdcLogOnTransmitComplete(void)
{
    if (s_initialized == 0u) {
        return;
    }
    const UBaseType_t saved = taskENTER_CRITICAL_FROM_ISR();
    volatile SRNOS_DbgUSBCdc *watch = log_watch();
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

void SRNOS_USBCdcLogOnDisconnect(void)
{
    if (s_initialized == 0u) {
        return;
    }
    set_dtr(false, xPortIsInsideInterrupt() == pdTRUE);
}

void SRNOS_USBCdcLogOnControlLineState(uint16_t state)
{
    if (s_initialized == 0u) {
        return;
    }
    set_dtr((state & 0x0001u) != 0u,
            xPortIsInsideInterrupt() == pdTRUE);
}
