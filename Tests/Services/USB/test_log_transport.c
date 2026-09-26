#include "omx_usb_cdc_log_transport.h"

#include "omx_boot_diag.h"
#include "SirinEmbedOS/Diagnostics/Watch/omx_debug_watch.h"
#include "omx_usb_cdc_channels.h"
#include "usbd_cdc_acm_if.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

USBD_HandleTypeDef hUsbDevice;
volatile omx_dbg_usb_cdc_t g_omx_dbg_usb_cdc[OMX_USB_CDC_CHANNEL_COUNT];

static const uint8_t s_boot[] = "BOOT-1\r\nBOOT-2\r\n";
static size_t s_boot_offset;
static uint32_t s_boot_resets;
static uint8_t s_tx_status = USBD_OK;
static uint8_t s_last_channel;
static uint8_t s_last_tx[384];
static size_t s_last_tx_length;
static uint32_t s_tx_calls;

void omx_usb_cdc_channels_init(void)
{
}

void omx_boot_diag_replay_reset(void)
{
    s_boot_offset = 0u;
    ++s_boot_resets;
}

bool omx_boot_diag_replay_peek(const uint8_t **data, size_t *length)
{
    if ((data == NULL) || (length == NULL) ||
        (s_boot_offset >= sizeof(s_boot) - 1u)) {
        return false;
    }
    size_t end = s_boot_offset;
    while ((end < sizeof(s_boot) - 1u) && (s_boot[end] != '\n')) {
        ++end;
    }
    if (end < sizeof(s_boot) - 1u) {
        ++end;
    }
    *data = &s_boot[s_boot_offset];
    *length = end - s_boot_offset;
    return true;
}

bool omx_boot_diag_replay_commit(size_t length)
{
    const uint8_t *data = NULL;
    size_t pending = 0u;
    if (!omx_boot_diag_replay_peek(&data, &pending) || (pending != length)) {
        return false;
    }
    (void)data;
    s_boot_offset += length;
    return true;
}

uint8_t CDC_Transmit(uint8_t ch, uint8_t *data, uint16_t length)
{
    ++s_tx_calls;
    s_last_channel = ch;
    s_last_tx_length = length;
    assert(length <= sizeof(s_last_tx));
    memcpy(s_last_tx, data, length);
    return s_tx_status;
}

static void expect_tx(const char *text)
{
    const size_t length = strlen(text);
    assert(s_last_channel == OMX_USB_CDC_LOG_CHANNEL);
    assert(s_last_tx_length == length);
    assert(memcmp(s_last_tx, text, length) == 0);
}

static void complete_and_service(void)
{
    omx_usb_cdc_log_on_transmit_complete();
    omx_usb_cdc_log_service(NULL);
}

int main(void)
{
    hUsbDevice.dev_state = USBD_STATE_CONFIGURED;
    omx_usb_cdc_log_transport_init();

    assert(!omx_usb_cdc_log_submit(NULL, (const uint8_t *)"closed", 6u));
    assert(g_omx_dbg_usb_cdc[0].queue_depth == 0u);

    omx_usb_cdc_log_on_control_line_state(1u);
    assert(omx_usb_cdc_log_is_connected());
    omx_usb_cdc_log_service(NULL);
    expect_tx("BOOT-1\r\n");
    assert(s_boot_offset == 0u);
    assert(s_boot_resets == 1u);

    assert(omx_usb_cdc_log_submit(NULL, (const uint8_t *)"live\r\n", 6u));
    assert(g_omx_dbg_usb_cdc[0].queue_depth == 1u);
    complete_and_service();
    expect_tx("BOOT-2\r\n");
    assert(s_boot_offset == 8u);
    complete_and_service();
    expect_tx("live\r\n");
    assert(s_boot_offset == sizeof(s_boot) - 1u);
    complete_and_service();
    assert(g_omx_dbg_usb_cdc[0].queue_depth == 0u);

    omx_usb_cdc_log_on_control_line_state(0u);
    assert(!omx_usb_cdc_log_is_connected());
    assert(!omx_usb_cdc_log_submit(NULL, (const uint8_t *)"stale", 5u));
    omx_usb_cdc_log_on_control_line_state(1u);
    omx_usb_cdc_log_service(NULL);
    expect_tx("BOOT-1\r\n");
    assert(s_boot_offset == 0u);
    assert(s_boot_resets == 2u);

    for (unsigned i = 0u; i < 17u; ++i) {
        char line[16];
        const int length = snprintf(line, sizeof(line), "L%02u\r\n", i);
        assert(length > 0);
        assert(omx_usb_cdc_log_submit(NULL, (const uint8_t *)line,
                                      (size_t)length));
    }
    assert(g_omx_dbg_usb_cdc[0].queue_depth == 16u);
    assert(g_omx_dbg_usb_cdc[0].queue_high_water == 16u);

    complete_and_service();
    expect_tx("BOOT-2\r\n");
    complete_and_service();
    expect_tx("L01\r\n");

    omx_usb_cdc_log_on_control_line_state(0u);
    assert(g_omx_dbg_usb_cdc[0].queue_depth == 0u);
    assert(!omx_usb_cdc_log_is_connected());

    s_tx_status = USBD_BUSY;
    const uint32_t calls_before_busy = s_tx_calls;
    omx_usb_cdc_log_on_control_line_state(1u);
    omx_usb_cdc_log_service(NULL);
    assert(s_tx_calls == calls_before_busy + 1u);
    assert(s_boot_offset == 0u);
    assert(g_omx_dbg_usb_cdc[0].tx_busy != 0u);
    s_tx_status = USBD_OK;
    omx_usb_cdc_log_service(NULL);
    expect_tx("BOOT-1\r\n");

    puts("USB CDC0 log transport host tests passed");
    return 0;
}
