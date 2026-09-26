#include "SirinEmbedOS/Services/Services.h"
#include "usbd_core.h"
#include "usbd_composite.h"
#include "usbd_desc.h"
#include "usb_otg.h"
#include "omx_usb_cdc_channels.h"
#include "omx_usb_cdc_log_transport.h"
#include "SirinEmbedOS/Services/Logging/bsp_srn_debug_port.h"
USBD_HandleTypeDef hUsbDevice;
bool SRNOS_USBStart(void)
{
    static bool started;
    if (started || hpcd_USB_OTG_HS.Instance != USB_OTG_HS) return false;
    /* CubeMX's HAL-only H723 template omitted the embedded PHY DP/DM pins.
     * This fixed-board glue owns the missing mux until regeneration supplies it. */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef gpio = { .Pin=GPIO_PIN_11|GPIO_PIN_12, .Mode=GPIO_MODE_AF_PP,
        .Pull=GPIO_NOPULL, .Speed=GPIO_SPEED_FREQ_VERY_HIGH, .Alternate=GPIO_AF10_OTG1_HS };
    HAL_GPIO_Init(GPIOA,&gpio);
    omx_usb_cdc_channels_init();
    omx_usb_cdc_log_transport_init();
    SRN_DebugPort_Init();
    USBD_COMPOSITE_Mount_Class();
    if (USBD_Init(&hUsbDevice,&USBD_Desc,DEVICE_HS)!=USBD_OK ||
        USBD_RegisterClass(&hUsbDevice,&USBD_COMPOSITE)!=USBD_OK ||
        USBD_CDC_ACM_RegisterInterface(&hUsbDevice,&USBD_CDC_ACM_fops)!=USBD_OK ||
        USBD_Start(&hUsbDevice)!=USBD_OK) {
        (void)USBD_DeInit(&hUsbDevice); return false;
    }
    started=true; return true;
}
