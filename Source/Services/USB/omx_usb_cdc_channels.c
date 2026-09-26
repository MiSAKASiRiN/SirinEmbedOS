#include "omx_usb_cdc_channels.h"

#include "SirinEmbedOS/Core/Memory/mem_sections.h"
#include "SirinEmbedOS/Diagnostics/Watch/omx_debug_watch.h"
#include "stm32h7xx.h"

OMX_DEBUG_WATCH_BSS volatile omx_dbg_usb_cdc_t
    g_omx_dbg_usb_cdc[OMX_USB_CDC_CHANNEL_COUNT];

static uint8_t s_initialized;

void omx_usb_cdc_channels_init(void)
{
    const uint32_t key = __get_PRIMASK();
    __disable_irq();
    if (s_initialized != 0u) {
        __set_PRIMASK(key);
        return;
    }
    omx_dbg_zero(g_omx_dbg_usb_cdc, sizeof(g_omx_dbg_usb_cdc));
    for (uint16_t channel = 0u; channel < OMX_USB_CDC_CHANNEL_COUNT; ++channel) {
        omx_dbg_init(&g_omx_dbg_usb_cdc[channel].h,
                     OMX_DBG_MODULE_USB_CDC,
                     channel,
                     (uint16_t)sizeof(g_omx_dbg_usb_cdc[channel]));
    }
    s_initialized = 1u;
    __set_PRIMASK(key);
}

const char *omx_usb_cdc_interface_label(uint8_t cdc_ch)
{
    static const char *const labels[OMX_USB_CDC_CHANNEL_COUNT] = {
        OMX_USB_CDC_LOG_INTERFACE_STRING,
        OMX_USB_CDC_SRN_LINK_INTERFACE_STRING,
    };

    return (cdc_ch < OMX_USB_CDC_CHANNEL_COUNT) ? labels[cdc_ch] : NULL;
}
