#include "usb_cdc_log_transport.h"

#include "boot_diag.h"
#include "SirinEmbedOS/Diagnostics/Watch/Watch.h"
#include "usb_cdc_channels.h"
#include "usbd_cdc_acm_if.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

USBD_HandleTypeDef SRNOS_USBDevice;
volatile SRNOS_DbgUSBCdc SRNOS_DebugDbgUSBCdc[SRNOS_USB_CDC_CHANNEL_COUNT];

static const uint8_t s_boot[] = "BOOT-1\r\nBOOT-2\r\n";
static size_t s_boot_offset;
static uint32_t s_boot_resets;
static uint8_t s_tx_status = USBD_OK;
static uint8_t s_last_channel;
static uint8_t s_last_tx[384];
static size_t s_last_tx_length;
static uint32_t s_tx_calls;

void SRNOS_USBCdcChannelsInit(void)
{
}

void SRNOS_BootDiagReplayReset(void)
{
    s_boot_offset = 0u;
    ++s_boot_resets;
}

bool SRNOS_BootDiagReplayPeek(const uint8_t **data, size_t *length)
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

bool SRNOS_BootDiagReplayCommit(size_t length)
{
    const uint8_t *data = NULL;
    size_t pending = 0u;
    if (!SRNOS_BootDiagReplayPeek(&data, &pending) || (pending != length)) {
        return false;
    }
    (void)data;
    s_boot_offset += length;
    return true;
}

uint8_t SRNOS_USBCdcTransmit(uint8_t ch, uint8_t *data, uint16_t length)
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
    assert(s_last_channel == SRNOS_USB_CDC_LOG_CHANNEL);
    assert(s_last_tx_length == length);
    assert(memcmp(s_last_tx, text, length) == 0);
}

static void complete_and_service(void)
{
    SRNOS_USBCdcLogOnTransmitComplete();
    SRNOS_USBCdcLogService(NULL);
}

int main(void)
{
    SRNOS_USBDevice.dev_state = USBD_STATE_CONFIGURED;
    SRNOS_USBCdcLogTransportInit();

    assert(!SRNOS_USBCdcLogSubmit(NULL, (const uint8_t *)"closed", 6u));
    assert(SRNOS_DebugDbgUSBCdc[0].queue_depth == 0u);

    SRNOS_USBCdcLogOnControlLineState(1u);
    assert(SRNOS_USBCdcLogIsConnected());
    SRNOS_USBCdcLogService(NULL);
    expect_tx("BOOT-1\r\n");
    assert(s_boot_offset == 0u);
    assert(s_boot_resets == 1u);

    assert(SRNOS_USBCdcLogSubmit(NULL, (const uint8_t *)"live\r\n", 6u));
    assert(SRNOS_DebugDbgUSBCdc[0].queue_depth == 1u);
    complete_and_service();
    expect_tx("BOOT-2\r\n");
    assert(s_boot_offset == 8u);
    complete_and_service();
    expect_tx("live\r\n");
    assert(s_boot_offset == sizeof(s_boot) - 1u);
    complete_and_service();
    assert(SRNOS_DebugDbgUSBCdc[0].queue_depth == 0u);

    SRNOS_USBCdcLogOnControlLineState(0u);
    assert(!SRNOS_USBCdcLogIsConnected());
    assert(!SRNOS_USBCdcLogSubmit(NULL, (const uint8_t *)"stale", 5u));
    SRNOS_USBCdcLogOnControlLineState(1u);
    SRNOS_USBCdcLogService(NULL);
    expect_tx("BOOT-1\r\n");
    assert(s_boot_offset == 0u);
    assert(s_boot_resets == 2u);

    for (unsigned i = 0u; i < 17u; ++i) {
        char line[16];
        const int length = snprintf(line, sizeof(line), "L%02u\r\n", i);
        assert(length > 0);
        assert(SRNOS_USBCdcLogSubmit(NULL, (const uint8_t *)line,
                                      (size_t)length));
    }
    assert(SRNOS_DebugDbgUSBCdc[0].queue_depth == 16u);
    assert(SRNOS_DebugDbgUSBCdc[0].queue_high_water == 16u);

    complete_and_service();
    expect_tx("BOOT-2\r\n");
    complete_and_service();
    expect_tx("L01\r\n");

    SRNOS_USBCdcLogOnControlLineState(0u);
    assert(SRNOS_DebugDbgUSBCdc[0].queue_depth == 0u);
    assert(!SRNOS_USBCdcLogIsConnected());

    s_tx_status = USBD_BUSY;
    const uint32_t calls_before_busy = s_tx_calls;
    SRNOS_USBCdcLogOnControlLineState(1u);
    SRNOS_USBCdcLogService(NULL);
    assert(s_tx_calls == calls_before_busy + 1u);
    assert(s_boot_offset == 0u);
    assert(SRNOS_DebugDbgUSBCdc[0].tx_busy != 0u);
    s_tx_status = USBD_OK;
    SRNOS_USBCdcLogService(NULL);
    expect_tx("BOOT-1\r\n");

    puts("USB CDC0 log transport host tests passed");
    return 0;
}
