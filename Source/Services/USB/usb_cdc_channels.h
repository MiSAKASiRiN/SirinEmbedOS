#ifndef SRNOS_USB_CDC_CHANNELS_H
#define SRNOS_USB_CDC_CHANNELS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SRNOS_USB_CDC_LOG_CHANNEL = 0u,
    SRNOS_USB_CDC_SIRIN_LINK_CHANNEL = 1u,
    SRNOS_USB_CDC_CHANNEL_COUNT = 2u,
};

enum {
    SRNOS_USB_CDC0_IN_EP = 0x81u,
    SRNOS_USB_CDC0_CMD_EP = 0x82u,
    SRNOS_USB_CDC0_OUT_EP = 0x01u,
    SRNOS_USB_CDC1_IN_EP = 0x83u,
    SRNOS_USB_CDC1_CMD_EP = 0x84u,
    SRNOS_USB_CDC1_OUT_EP = 0x02u,
};

#define SRNOS_USB_CDC_LOG_INTERFACE_STRING \
    "OmniX OmniCtrl 2 Pro LOG"
#define SRNOS_USB_CDC_SIRIN_LINK_INTERFACE_STRING \
    "OmniX OmniCtrl 2 Pro SRN-LNK"

const char *SRNOS_USBCdcInterfaceLabel(uint8_t cdc_ch);
void SRNOS_USBCdcChannelsInit(void);

#ifdef __cplusplus
}
#endif

#endif /* SRNOS_USB_CDC_CHANNELS_H */
