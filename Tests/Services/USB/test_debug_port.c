#include "SirinEmbedOS/Services/Logging/bsp_srn_debug_port.h"
#include "SirinEmbedOS/Diagnostics/Watch/omx_debug_watch.h"
#include "usbd_cdc_acm_if.h"
#include <assert.h>
#include <stdio.h>
int test_isr;
USBD_HandleTypeDef hUsbDevice={.dev_state=USBD_STATE_CONFIGURED};
volatile omx_dbg_srn_log_t g_omx_dbg_srn_log;
static const uint8_t *active_buffer;
uint32_t HAL_GetTick(void) {return 0;}
void SRN_Log_Init(void) {}
uint8_t CDC_Transmit(uint8_t channel,uint8_t *data,uint16_t length)
{ assert(channel==1 && length); active_buffer=data;return USBD_OK; }
int main(void)
{
    uint8_t a[72]={1},b[72]={2};bool success;
    SRN_DebugPort_Init();SRN_DebugPort_OnUsbControlLineState(1);
    assert(SRN_DebugPort_TryTransmit(a,sizeof(a))==SRN_DEBUG_PORT_WRITE_OK);
    test_isr=1;SRN_DebugPort_OnUsbControlLineState(0);
    assert(SRN_DebugPort_TxBusy() && !SRN_DebugPort_TakeTxComplete(&success));
    uint32_t generation=SRN_DebugPort_GetSessionGeneration();assert(generation==1);
    SRN_DebugPort_OnUsbControlLineState(1);test_isr=0;
    assert(SRN_DebugPort_TryTransmit(b,sizeof(b))==SRN_DEBUG_PORT_WRITE_FULL && active_buffer==a);
    test_isr=1;SRN_DebugPort_OnUsbTransmitComplete();test_isr=0;
    assert(SRN_DebugPort_TakeTxComplete(&success) && success);
    assert(SRN_DebugPort_TryTransmit(b,sizeof(b))==SRN_DEBUG_PORT_WRITE_OK && active_buffer==b);
    test_isr=1;SRN_DebugPort_OnUsbDisconnect();test_isr=0; /* class already closed endpoints */
    assert(!SRN_DebugPort_TxBusy() && SRN_DebugPort_TakeTxComplete(&success) && !success);
    assert(SRN_DebugPort_GetSessionGeneration()==generation+1);
    puts("Actual CDC1 backend: DTR close/reopen preserves in-flight buffer; completion and closed endpoint reclaim passed");
}
