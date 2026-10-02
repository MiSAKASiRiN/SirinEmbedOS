#include "USB/usb_board.h"
#include "usb_otg.h"
bool SRNOS_BoardUSBPrepare(void)
{
    if (hpcd_USB_OTG_HS.Instance != USB_OTG_HS) return false;
    /* CubeMX's HAL-only H723 template omitted the embedded PHY DP/DM pins.
     * This board support owns the missing mux until regeneration supplies it. */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef gpio = { .Pin=GPIO_PIN_11|GPIO_PIN_12, .Mode=GPIO_MODE_AF_PP,
        .Pull=GPIO_NOPULL, .Speed=GPIO_SPEED_FREQ_VERY_HIGH, .Alternate=GPIO_AF10_OTG1_HS };
    HAL_GPIO_Init(GPIOA,&gpio);
    return true;
}
