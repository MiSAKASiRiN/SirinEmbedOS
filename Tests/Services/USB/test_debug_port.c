#include "Services/Logging/debug_port_internal.h"
#include "SirinEmbedOS/Diagnostics/Watch/Watch.h"
#include "usbd_cdc_acm_if.h"
#include <assert.h>
#include <stdio.h>
int test_isr;
USBD_HandleTypeDef SRNOS_USBDevice={.dev_state=USBD_STATE_CONFIGURED};
volatile SRNOS_DbgLog SRNOS_DebugLog;
static const uint8_t *active_buffer;
uint32_t HAL_GetTick(void) {return 0;}
void SRNOS_LogInit(void) {}
uint8_t SRNOS_USBCdcTransmit(uint8_t channel,uint8_t *data,uint16_t length)
{ assert(channel==1 && length); active_buffer=data;return USBD_OK; }
int main(void)
{
    uint8_t a[72]={1},b[72]={2};bool success;
    SRNOS_DebugPortInit();SRNOS_DebugPortOnUsbControlLineState(1);
    assert(SRNOS_DebugPortTryTransmit(a,sizeof(a))==SRNOS_DEBUG_PORT_WRITE_OK);
    test_isr=1;SRNOS_DebugPortOnUsbControlLineState(0);
    assert(SRNOS_DebugPortTxBusy() && !SRNOS_DebugPortTakeTxComplete(&success));
    uint32_t generation=SRNOS_DebugPortGetSessionGeneration();assert(generation==1);
    SRNOS_DebugPortOnUsbControlLineState(1);test_isr=0;
    assert(SRNOS_DebugPortTryTransmit(b,sizeof(b))==SRNOS_DEBUG_PORT_WRITE_FULL && active_buffer==a);
    test_isr=1;SRNOS_DebugPortOnUsbTransmitComplete();test_isr=0;
    assert(SRNOS_DebugPortTakeTxComplete(&success) && success);
    assert(SRNOS_DebugPortTryTransmit(b,sizeof(b))==SRNOS_DEBUG_PORT_WRITE_OK && active_buffer==b);
    test_isr=1;SRNOS_DebugPortOnUsbDisconnect();test_isr=0; /* class already closed endpoints */
    assert(!SRNOS_DebugPortTxBusy() && SRNOS_DebugPortTakeTxComplete(&success) && !success);
    assert(SRNOS_DebugPortGetSessionGeneration()==generation+1);
    puts("Actual CDC1 backend: DTR close/reopen preserves in-flight buffer; completion and closed endpoint reclaim passed");
}
