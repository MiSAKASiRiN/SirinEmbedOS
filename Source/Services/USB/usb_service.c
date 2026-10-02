#include "SirinEmbedOS/Services/Services.h"
#include "usbd_core.h"
#include "usbd_composite.h"
#include "usbd_desc.h"
#include "USB/usb_board.h"
#include "usb_cdc_channels.h"
#include "usb_cdc_log_transport.h"
#include "Services/Logging/debug_port_internal.h"
USBD_HandleTypeDef SRNOS_USBDevice;
bool SRNOS_USBStart(void)
{
    static bool started;
    if (started || !SRNOS_BoardUSBPrepare()) return false;
    SRNOS_USBCdcChannelsInit();
    SRNOS_USBCdcLogTransportInit();
    SRNOS_DebugPortInit();
    USBD_COMPOSITE_Mount_Class();
    if (USBD_Init(&SRNOS_USBDevice,&SRNOS_USBDescriptors,SRNOS_USB_SPEED_HS)!=USBD_OK ||
        USBD_RegisterClass(&SRNOS_USBDevice,&USBD_COMPOSITE)!=USBD_OK ||
        USBD_CDC_ACM_RegisterInterface(&SRNOS_USBDevice,&SRNOS_USBCdcInterface)!=USBD_OK ||
        USBD_Start(&SRNOS_USBDevice)!=USBD_OK) {
        (void)USBD_DeInit(&SRNOS_USBDevice); return false;
    }
    started=true; return true;
}
