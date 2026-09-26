#ifndef OMX_USB_CDC_CHANNELS_H
#define OMX_USB_CDC_CHANNELS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OMX_USB_CDC_LOG_CHANNEL = 0u,
    OMX_USB_CDC_SRN_LINK_CHANNEL = 1u,
    OMX_USB_CDC_CHANNEL_COUNT = 2u,
};

enum {
    OMX_USB_CDC0_IN_EP = 0x81u,
    OMX_USB_CDC0_CMD_EP = 0x82u,
    OMX_USB_CDC0_OUT_EP = 0x01u,
    OMX_USB_CDC1_IN_EP = 0x83u,
    OMX_USB_CDC1_CMD_EP = 0x84u,
    OMX_USB_CDC1_OUT_EP = 0x02u,
};

#define OMX_USB_CDC_LOG_INTERFACE_STRING \
    "OmniX OmniCtrl 2 Pro LOG"
#define OMX_USB_CDC_SRN_LINK_INTERFACE_STRING \
    "OmniX OmniCtrl 2 Pro SRN-LNK"

const char *omx_usb_cdc_interface_label(uint8_t cdc_ch);
void omx_usb_cdc_channels_init(void);

#ifdef __cplusplus
}
#endif

#endif /* OMX_USB_CDC_CHANNELS_H */
