#include "usb_cdc_channels.h"

#include "SirinEmbedOS/Core/Memory/MemorySections.h"
#include "SirinEmbedOS/Diagnostics/Watch/Watch.h"
#include "stm32h7xx.h"

SRNOS_DEBUG_WATCH_BSS volatile SRNOS_DbgUSBCdc
    SRNOS_DebugDbgUSBCdc[SRNOS_USB_CDC_CHANNEL_COUNT];

static uint8_t s_initialized;

void SRNOS_USBCdcChannelsInit(void)
{
    const uint32_t key = __get_PRIMASK();
    __disable_irq();
    if (s_initialized != 0u) {
        __set_PRIMASK(key);
        return;
    }
    SRNOS_DbgZero(SRNOS_DebugDbgUSBCdc, sizeof(SRNOS_DebugDbgUSBCdc));
    for (uint16_t channel = 0u; channel < SRNOS_USB_CDC_CHANNEL_COUNT; ++channel) {
        SRNOS_DbgInit(&SRNOS_DebugDbgUSBCdc[channel].h,
                     SRNOS_DBG_MODULE_USB_CDC,
                     channel,
                     (uint16_t)sizeof(SRNOS_DebugDbgUSBCdc[channel]));
    }
    s_initialized = 1u;
    __set_PRIMASK(key);
}

const char *SRNOS_USBCdcInterfaceLabel(uint8_t cdc_ch)
{
    static const char *const labels[SRNOS_USB_CDC_CHANNEL_COUNT] = {
        SRNOS_USB_CDC_LOG_INTERFACE_STRING,
        SRNOS_USB_CDC_HOST_LINK_INTERFACE_STRING,
    };

    return (cdc_ch < SRNOS_USB_CDC_CHANNEL_COUNT) ? labels[cdc_ch] : NULL;
}
