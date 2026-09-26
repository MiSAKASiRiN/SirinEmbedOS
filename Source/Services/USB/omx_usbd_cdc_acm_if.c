#include <usbd_cdc_acm_if.h>

#include "SirinEmbedOS/Core/C/Bounds.h"
#include "SirinEmbedOS/Core/C/Endian.h"
#include "SirinEmbedOS/Core/C/Memory.h"
#include "SirinEmbedOS/Services/Logging/bsp_srn_debug_port.h"
#include "SirinEmbedOS/Diagnostics/Watch/omx_debug_watch.h"
#include "omx_usb_cdc_channels.h"
#include "omx_usb_cdc_log_transport.h"

#include <stddef.h>

extern USBD_HandleTypeDef hUsbDevice;

#define OMX_USB_CDC_ACM_RX_SIZE 512U
#define OMX_USB_CDC_ACM_TX_SIZE 512U

/* The STM32 PCD FIFO path performs word-wide accesses even with USB DMA off.
 * Keep class-owned buffers aligned and stable until the async TX callback. */
static _Alignas(uint32_t)
uint8_t s_rx_buffer[NUMBER_OF_CDC][OMX_USB_CDC_ACM_RX_SIZE];
static _Alignas(uint32_t)
uint8_t s_tx_buffer[NUMBER_OF_CDC][OMX_USB_CDC_ACM_TX_SIZE];
static USBD_CDC_ACM_LineCodingTypeDef s_line_coding[NUMBER_OF_CDC];
static volatile uint8_t s_dtr[NUMBER_OF_CDC];
static volatile uint16_t s_tx_length[NUMBER_OF_CDC];

_Static_assert(NUMBER_OF_CDC == OMX_USB_CDC_CHANNEL_COUNT,
               "AL94 CDC count must match the project channel map");
_Static_assert(OMX_USB_CDC_LOG_CHANNEL == 0u,
               "CDC0 is reserved for project ASCII logs");
_Static_assert(OMX_USB_CDC_SRN_LINK_CHANNEL == 1u,
               "CDC1 is reserved for SRN-Link V2");
_Static_assert((OMX_USB_CDC_ACM_RX_SIZE % sizeof(uint32_t)) == 0u,
               "Each CDC RX staging buffer must remain word aligned");
_Static_assert((OMX_USB_CDC_ACM_TX_SIZE % sizeof(uint32_t)) == 0u,
               "Each CDC TX staging buffer must remain word aligned");

static int8_t CDC_Init(uint8_t cdc_ch);
static int8_t CDC_DeInit(uint8_t cdc_ch);
static int8_t CDC_Control(uint8_t cdc_ch, uint8_t cmd, uint8_t *pbuf, uint16_t length);
static int8_t CDC_Receive(uint8_t cdc_ch, uint8_t *Buf, uint32_t *Len);
static int8_t CDC_TransmitCplt(uint8_t cdc_ch, uint8_t *Buf, uint32_t *Len, uint8_t epnum);

USBD_CDC_ACM_ItfTypeDef USBD_CDC_ACM_fops = {
    CDC_Init,
    CDC_DeInit,
    CDC_Control,
    CDC_Receive,
    CDC_TransmitCplt,
};

static void set_default_line_coding(uint8_t cdc_ch)
{
    if (!SRNOS_Bounds_IndexValid(cdc_ch, NUMBER_OF_CDC)) {
        return;
    }
    if (s_line_coding[cdc_ch].bitrate == 0U) {
        s_line_coding[cdc_ch].bitrate = 115200U;
        s_line_coding[cdc_ch].format = 0U;
        s_line_coding[cdc_ch].paritytype = 0U;
        s_line_coding[cdc_ch].datatype = 8U;
    }
}

static int8_t CDC_Init(uint8_t cdc_ch)
{
    static const uint8_t expected_in[OMX_USB_CDC_CHANNEL_COUNT] = {
        OMX_USB_CDC0_IN_EP, OMX_USB_CDC1_IN_EP,
    };
    static const uint8_t expected_cmd[OMX_USB_CDC_CHANNEL_COUNT] = {
        OMX_USB_CDC0_CMD_EP, OMX_USB_CDC1_CMD_EP,
    };
    static const uint8_t expected_out[OMX_USB_CDC_CHANNEL_COUNT] = {
        OMX_USB_CDC0_OUT_EP, OMX_USB_CDC1_OUT_EP,
    };
    if (!SRNOS_Bounds_IndexValid(cdc_ch, NUMBER_OF_CDC) ||
        (CDC_IN_EP[cdc_ch] != expected_in[cdc_ch]) ||
        (CDC_CMD_EP[cdc_ch] != expected_cmd[cdc_ch]) ||
        (CDC_OUT_EP[cdc_ch] != expected_out[cdc_ch])) {
        return (int8_t)USBD_FAIL;
    }

    omx_usb_cdc_channels_init();
    s_dtr[cdc_ch] = 0u;
    set_default_line_coding(cdc_ch);
    (void)USBD_CDC_SetRxBuffer(cdc_ch, &hUsbDevice, s_rx_buffer[cdc_ch]);
    (void)USBD_CDC_ReceivePacket(cdc_ch, &hUsbDevice);
    if (cdc_ch == OMX_USB_CDC_LOG_CHANNEL) {
        omx_usb_cdc_log_transport_init();
    }
    g_omx_dbg_usb_cdc[cdc_ch].configured = 1u;
    g_omx_dbg_usb_cdc[cdc_ch].dtr = 0u;
    return (int8_t)USBD_OK;
}

static int8_t CDC_DeInit(uint8_t cdc_ch)
{
    if (!SRNOS_Bounds_IndexValid(cdc_ch, NUMBER_OF_CDC)) {
        return (int8_t)USBD_FAIL;
    }
    const uint8_t was_dtr = s_dtr[cdc_ch];
    s_dtr[cdc_ch] = 0u;
    s_tx_length[cdc_ch] = 0u;
    g_omx_dbg_usb_cdc[cdc_ch].configured = 0u;
    g_omx_dbg_usb_cdc[cdc_ch].dtr = 0u;
    if (was_dtr != 0u) {
        g_omx_dbg_usb_cdc[cdc_ch].disconnects++;
    }
    if (cdc_ch == OMX_USB_CDC_LOG_CHANNEL) {
        omx_usb_cdc_log_on_disconnect();
    } else if (cdc_ch == OMX_USB_CDC_SRN_LINK_CHANNEL) {
        SRN_DebugPort_OnUsbDisconnect();
    }
    return (int8_t)USBD_OK;
}

static int8_t CDC_Control(uint8_t cdc_ch, uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
    if (!SRNOS_Bounds_IndexValid(cdc_ch, NUMBER_OF_CDC)) {
        return (int8_t)USBD_FAIL;
    }

    switch (cmd) {
    case CDC_SET_LINE_CODING:
        if ((pbuf != NULL) && (length >= 7U) &&
            SRNOS_Endian_LoadLE32(SRNOS_Memory_Bytes(pbuf, length), 0u,
                               &s_line_coding[cdc_ch].bitrate)) {
            s_line_coding[cdc_ch].format = pbuf[4];
            s_line_coding[cdc_ch].paritytype = pbuf[5];
            s_line_coding[cdc_ch].datatype = pbuf[6];
        }
        break;

    case CDC_GET_LINE_CODING:
        if ((pbuf != NULL) && (length >= 7U)) {
            set_default_line_coding(cdc_ch);
            if (SRNOS_Endian_StoreLE32(SRNOS_Memory_MutableBytes(pbuf, length),
                                    0u,
                                    s_line_coding[cdc_ch].bitrate)) {
                pbuf[4] = s_line_coding[cdc_ch].format;
                pbuf[5] = s_line_coding[cdc_ch].paritytype;
                pbuf[6] = s_line_coding[cdc_ch].datatype;
            }
        }
        break;

    case CDC_SET_CONTROL_LINE_STATE:
        if ((pbuf != NULL) && (length == 0U)) {
            uint16_t state = 0u;
            if (!SRNOS_Endian_LoadLE16(
                    SRNOS_Memory_Bytes(pbuf, sizeof(USBD_SetupReqTypedef)),
                    offsetof(USBD_SetupReqTypedef, wValue),
                    &state)) {
                return (int8_t)USBD_FAIL;
            }
            const uint8_t was_dtr = s_dtr[cdc_ch];
            s_dtr[cdc_ch] = ((state & 0x0001u) != 0u) ? 1u : 0u;
            g_omx_dbg_usb_cdc[cdc_ch].dtr = s_dtr[cdc_ch];
            if ((was_dtr != 0u) && (s_dtr[cdc_ch] == 0u)) {
                g_omx_dbg_usb_cdc[cdc_ch].disconnects++;
                g_omx_dbg_usb_cdc[cdc_ch].tx_active = 0u;
                s_tx_length[cdc_ch] = 0u;
            }
            if (cdc_ch == OMX_USB_CDC_LOG_CHANNEL) {
                omx_usb_cdc_log_on_control_line_state(state);
            } else if (cdc_ch == OMX_USB_CDC_SRN_LINK_CHANNEL) {
                SRN_DebugPort_OnUsbControlLineState(state);
            }
        }
        break;

    default:
        break;
    }

    return (int8_t)USBD_OK;
}

static int8_t CDC_Receive(uint8_t cdc_ch, uint8_t *Buf, uint32_t *Len)
{
    if (!SRNOS_Bounds_IndexValid(cdc_ch, NUMBER_OF_CDC)) {
        return (int8_t)USBD_FAIL;
    }
    if ((Buf != NULL) && (Len != NULL)) {
        if (cdc_ch == OMX_USB_CDC_LOG_CHANNEL) {
            omx_usb_cdc_log_on_receive(*Len);
        } else if (cdc_ch == OMX_USB_CDC_SRN_LINK_CHANNEL) {
            g_omx_dbg_usb_cdc[cdc_ch].rx_events++;
            g_omx_dbg_usb_cdc[cdc_ch].rx_bytes += *Len;
            g_omx_dbg_usb_cdc[cdc_ch].last_rx_len = *Len;
            SRN_DebugPort_OnUsbReceive(Buf, *Len);
        }
    }

    (void)USBD_CDC_SetRxBuffer(cdc_ch, &hUsbDevice, s_rx_buffer[cdc_ch]);
    (void)USBD_CDC_ReceivePacket(cdc_ch, &hUsbDevice);
    return (int8_t)USBD_OK;
}

static int8_t CDC_TransmitCplt(uint8_t cdc_ch, uint8_t *Buf, uint32_t *Len, uint8_t epnum)
{
    (void)Buf;
    (void)Len;
    (void)epnum;
    if (!SRNOS_Bounds_IndexValid(cdc_ch, NUMBER_OF_CDC)) {
        return (int8_t)USBD_FAIL;
    }
    if (cdc_ch == OMX_USB_CDC_LOG_CHANNEL) {
        omx_usb_cdc_log_on_transmit_complete();
    } else if (cdc_ch == OMX_USB_CDC_SRN_LINK_CHANNEL) {
        g_omx_dbg_usb_cdc[cdc_ch].tx_active = 0u;
        if ((s_dtr[cdc_ch] != 0u) && (s_tx_length[cdc_ch] != 0u)) {
            g_omx_dbg_usb_cdc[cdc_ch].tx_complete++;
            g_omx_dbg_usb_cdc[cdc_ch].tx_bytes += s_tx_length[cdc_ch];
        }
        s_tx_length[cdc_ch] = 0u;
        SRN_DebugPort_OnUsbTransmitComplete();
    }
    return (int8_t)USBD_OK;
}

uint8_t CDC_Transmit(uint8_t ch, uint8_t *Buf, uint16_t Len)
{
    extern USBD_CDC_ACM_HandleTypeDef CDC_ACM_Class_Data[];

    if (!SRNOS_Bounds_IndexValid(ch, NUMBER_OF_CDC) ||
        (Buf == NULL) || (Len == 0U)) {
        return (uint8_t)USBD_FAIL;
    }
    if ((hUsbDevice.dev_state != USBD_STATE_CONFIGURED) ||
        (s_dtr[ch] == 0u)) {
        g_omx_dbg_usb_cdc[ch].tx_drop += Len;
        return (uint8_t)USBD_FAIL;
    }

    USBD_CDC_ACM_HandleTypeDef *hcdc = &CDC_ACM_Class_Data[ch];
    if (hcdc->TxState != 0U) {
        g_omx_dbg_usb_cdc[ch].tx_busy++;
        return (uint8_t)USBD_BUSY;
    }

    if (!SRNOS_Bounds_RangeValid(0u, Len, sizeof(s_tx_buffer[ch])) ||
        !SRNOS_Memory_Copy(
            SRNOS_Memory_MutableBytes(s_tx_buffer[ch], sizeof(s_tx_buffer[ch])),
            SRNOS_Memory_Bytes(Buf, Len))) {
        g_omx_dbg_usb_cdc[ch].tx_errors++;
        g_omx_dbg_usb_cdc[ch].tx_drop += Len;
        return (uint8_t)USBD_FAIL;
    }

    (void)USBD_CDC_SetTxBuffer(ch, &hUsbDevice, s_tx_buffer[ch], Len);
    const uint8_t status = USBD_CDC_TransmitPacket(ch, &hUsbDevice);
    if ((status == USBD_OK) && (ch == OMX_USB_CDC_SRN_LINK_CHANNEL)) {
        g_omx_dbg_usb_cdc[ch].tx_active = 1u;
        g_omx_dbg_usb_cdc[ch].tx_events++;
        g_omx_dbg_usb_cdc[ch].last_tx_len = Len;
        s_tx_length[ch] = Len;
    } else if (status != USBD_OK) {
        g_omx_dbg_usb_cdc[ch].tx_errors++;
    }
    return status;
}
