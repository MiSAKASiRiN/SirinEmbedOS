#include "SirinEmbedOS/Diagnostics/Watch/omx_debug_watch.h"
#include "omx_usb_cdc_channels.h"
#include "omx_usb_cdc_log_transport.h"
#include "usbd_cdc_acm_if.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

USBD_HandleTypeDef hUsbDevice;
USBD_CDC_ACM_HandleTypeDef CDC_ACM_Class_Data[NUMBER_OF_CDC];
uint8_t CDC_IN_EP[NUMBER_OF_CDC] = {0x81u, 0x83u};
uint8_t CDC_CMD_EP[NUMBER_OF_CDC] = {0x82u, 0x84u};
uint8_t CDC_OUT_EP[NUMBER_OF_CDC] = {0x01u, 0x02u};
volatile omx_dbg_usb_cdc_t g_omx_dbg_usb_cdc[NUMBER_OF_CDC];

extern USBD_CDC_ACM_ItfTypeDef USBD_CDC_ACM_fops;

static uint32_t s_log_rx;
static uint32_t s_link_rx;
static uint32_t s_log_tx_complete;
static uint32_t s_link_tx_complete;
static uint32_t s_log_disconnect;
static uint32_t s_link_disconnect;
static uint16_t s_log_dtr;
static uint16_t s_link_dtr;
static uint32_t s_tx_packet_calls[NUMBER_OF_CDC];
static uint8_t *s_tx_data[NUMBER_OF_CDC];
static uint32_t s_tx_length[NUMBER_OF_CDC];

void omx_usb_cdc_channels_init(void)
{
}

void omx_usb_cdc_log_transport_init(void)
{
}

void omx_usb_cdc_log_on_receive(uint32_t length)
{
    s_log_rx += length;
}

void omx_usb_cdc_log_on_transmit_complete(void)
{
    ++s_log_tx_complete;
}

void omx_usb_cdc_log_on_disconnect(void)
{
    ++s_log_disconnect;
}

void omx_usb_cdc_log_on_control_line_state(uint16_t state)
{
    s_log_dtr = state;
}

void SRN_DebugPort_OnUsbReceive(const uint8_t *data, uint32_t length)
{
    assert(data != NULL);
    s_link_rx += length;
}

void SRN_DebugPort_OnUsbTransmitComplete(void)
{
    ++s_link_tx_complete;
}

void SRN_DebugPort_OnUsbDisconnect(void)
{
    ++s_link_disconnect;
}

void SRN_DebugPort_OnUsbControlLineState(uint16_t state)
{
    s_link_dtr = state;
}

uint8_t USBD_CDC_SetTxBuffer(uint8_t ch, USBD_HandleTypeDef *device,
                             uint8_t *data, uint32_t length)
{
    (void)device;
    assert(ch < NUMBER_OF_CDC);
    assert(data != NULL);
    assert(((uintptr_t)data % _Alignof(uint32_t)) == 0u);
    s_tx_data[ch] = data;
    s_tx_length[ch] = length;
    return USBD_OK;
}

uint8_t USBD_CDC_SetRxBuffer(uint8_t ch, USBD_HandleTypeDef *device,
                             uint8_t *data)
{
    (void)ch;
    (void)device;
    (void)data;
    return USBD_OK;
}

uint8_t USBD_CDC_ReceivePacket(uint8_t ch, USBD_HandleTypeDef *device)
{
    (void)ch;
    (void)device;
    return USBD_OK;
}

uint8_t USBD_CDC_TransmitPacket(uint8_t ch, USBD_HandleTypeDef *device)
{
    (void)device;
    ++s_tx_packet_calls[ch];
    return USBD_OK;
}

static void set_dtr(uint8_t channel, uint16_t state)
{
    USBD_SetupReqTypedef request = {0};
    request.wValue = state;
    assert(USBD_CDC_ACM_fops.Control(
               channel,
               CDC_SET_CONTROL_LINE_STATE,
               (uint8_t *)&request,
               0u) == (int8_t)USBD_OK);
}

int main(void)
{
    hUsbDevice.dev_state = USBD_STATE_CONFIGURED;
    assert(USBD_CDC_ACM_fops.Init(0u) == (int8_t)USBD_OK);
    assert(USBD_CDC_ACM_fops.Init(1u) == (int8_t)USBD_OK);

    uint8_t rx[] = {1u, 2u, 3u};
    uint32_t length = sizeof(rx);
    assert(USBD_CDC_ACM_fops.Receive(0u, rx, &length) == (int8_t)USBD_OK);
    assert(s_log_rx == sizeof(rx));
    assert(s_link_rx == 0u);
    assert(USBD_CDC_ACM_fops.Receive(1u, rx, &length) == (int8_t)USBD_OK);
    assert(s_link_rx == sizeof(rx));

    set_dtr(0u, 1u);
    assert(s_log_dtr == 1u);
    assert(s_link_dtr == 0u);
    _Alignas(uint32_t)
    uint8_t unaligned_source[] = {0xffu, 1u, 2u, 3u, 0xeeu};
    assert(((uintptr_t)&unaligned_source[1] % _Alignof(uint32_t)) != 0u);
    assert(CDC_Transmit(0u, &unaligned_source[1], 3u) == USBD_OK);
    assert(s_tx_data[0] != &unaligned_source[1]);
    assert(s_tx_length[0] == 3u);
    assert(s_tx_data[0][0] == 1u);
    assert(s_tx_data[0][1] == 2u);
    assert(s_tx_data[0][2] == 3u);
    unaligned_source[1] = 0xaau;
    assert(s_tx_data[0][0] == 1u);
    assert(CDC_Transmit(1u, rx, sizeof(rx)) == USBD_FAIL);

    set_dtr(1u, 1u);
    assert(s_link_dtr == 1u);
    assert(CDC_Transmit(1u, rx, sizeof(rx)) == USBD_OK);
    assert(s_tx_data[1] != rx);
    assert(s_tx_data[1] != s_tx_data[0]);
    assert(s_tx_length[1] == sizeof(rx));
    assert(s_tx_data[1][0] == 1u);
    assert(s_tx_data[1][1] == 2u);
    assert(s_tx_data[1][2] == 3u);
    CDC_ACM_Class_Data[1].TxState = 1u;
    assert(CDC_Transmit(1u, rx, sizeof(rx)) == USBD_BUSY);
    assert(CDC_Transmit(0u, rx, sizeof(rx)) == USBD_OK);

    uint8_t oversized[513] = {0};
    assert(CDC_Transmit(0u, oversized, sizeof(oversized)) == USBD_FAIL);

    assert(USBD_CDC_ACM_fops.TransmitCplt(0u, rx, &length, 1u) ==
           (int8_t)USBD_OK);
    assert(s_log_tx_complete == 1u);
    assert(s_link_tx_complete == 0u);
    assert(USBD_CDC_ACM_fops.TransmitCplt(1u, rx, &length, 3u) ==
           (int8_t)USBD_OK);
    assert(s_link_tx_complete == 1u);

    assert(USBD_CDC_ACM_fops.DeInit(0u) == (int8_t)USBD_OK);
    assert(USBD_CDC_ACM_fops.DeInit(1u) == (int8_t)USBD_OK);
    assert(s_log_disconnect == 1u);
    assert(s_link_disconnect == 1u);
    assert(s_tx_packet_calls[0] == 2u);
    assert(s_tx_packet_calls[1] == 1u);

    puts("USB dual CDC callback dispatch host tests passed");
    return 0;
}
