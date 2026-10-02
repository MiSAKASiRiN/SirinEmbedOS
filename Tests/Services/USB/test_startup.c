#include <assert.h>
#include <stdio.h>
#include "usb_otg.h"
#include "usbd_core.h"
#include "usbd_composite.h"
#include "SirinEmbedOS/Services/Services.h"
PCD_HandleTypeDef hpcd_USB_OTG_HS;
USBD_DescriptorsTypeDef SRNOS_USBDescriptors;
int USBD_COMPOSITE, SRNOS_USBCdcInterface;
static unsigned step, fail_at, deinits;
static void next(unsigned expected) { assert(++step==expected); }
void test_gpio_clock(void) { next(1); }
void HAL_GPIO_Init(void *port,const GPIO_InitTypeDef *g) {
    next(2); assert(port==GPIOA && g->Pin==(GPIO_PIN_11|GPIO_PIN_12));
    assert(g->Mode==GPIO_MODE_AF_PP && g->Pull==GPIO_NOPULL);
    assert(g->Speed==GPIO_SPEED_FREQ_VERY_HIGH && g->Alternate==GPIO_AF10_OTG1_HS);
}
void SRNOS_USBCdcChannelsInit(void) { next(3); }
void SRNOS_USBCdcLogTransportInit(void) { next(4); }
void SRNOS_DebugPortInit(void) { next(5); }
void USBD_COMPOSITE_Mount_Class(void) { next(6); }
int USBD_Init(USBD_HandleTypeDef *h,USBD_DescriptorsTypeDef *d,int speed) {
    (void)h; next(7); assert(d==&SRNOS_USBDescriptors && speed==SRNOS_USB_SPEED_HS); return fail_at==step;
}
int USBD_RegisterClass(USBD_HandleTypeDef *h,void *c) {
    (void)h; next(8); assert(c==&USBD_COMPOSITE); return fail_at==step;
}
int USBD_CDC_ACM_RegisterInterface(USBD_HandleTypeDef *h,void *f) {
    (void)h; next(9); assert(f==&SRNOS_USBCdcInterface); return fail_at==step;
}
int USBD_Start(USBD_HandleTypeDef *h) { (void)h; next(10); return fail_at==step; }
int USBD_DeInit(USBD_HandleTypeDef *h) { (void)h; ++deinits; return 0; }
int main(void) {
    assert(!SRNOS_USBStart() && step==0 && deinits==0);
    hpcd_USB_OTG_HS.Instance=USB_OTG_HS;
    for(fail_at=7;fail_at<=10;++fail_at) {
        step=0; assert(!SRNOS_USBStart()); assert(step==fail_at && deinits==fail_at-6);
    }
    step=0; fail_at=0; assert(SRNOS_USBStart() && step==10 && deinits==4);
    step=0; assert(!SRNOS_USBStart() && step==0 && deinits==4);
    puts("USB preparation, four startup failure paths, retry order and duplicate start passed");
}
