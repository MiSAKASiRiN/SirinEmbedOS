#include "SirinEmbedOS/Services/Services.h"
#include "SirinEmbedOS/Services/Logging/Worker/tskptt_log.h"
#include "omx_usb_cdc_log_transport.h"
static bool prepared;
static osThreadId_t worker;
static tskptt_log_config_t saved_config;
bool SRNOS_LoggingPrepare(uint32_t mask, SRNOS_SerialChannel *uart)
{
    if (prepared || osKernelGetState() == osKernelRunning) return false;
    if (((mask & SRN_LOG_BACKEND_UART) != 0u) != (uart != NULL)) return false;
    if (!SRNOS_LogBindUart(uart)) return false;
    const srn_log_usb_transport_t usb = { NULL, omx_usb_cdc_log_submit, omx_usb_cdc_log_service };
    if (mask & SRN_LOG_BACKEND_USB_CDC) {
        SRN_Log_RegisterUsbTransport(&usb);
    }
    saved_config = TSKPTT_LOG_DEFAULT_CONFIG;
    saved_config.backend_mask = mask;
    SRN_Log_SetBackendMask(mask);
    SRNOS_LogFreezeTransports();
    prepared = true;
    return true;
}
bool SRNOS_LoggingStart(void)
{
    if (!prepared || worker != NULL) return false;
    if (!TSKPTT_Log_InitResources(&saved_config)) return false;
    const osThreadAttr_t attr = { .name="Logging", .stack_size=2048u, .priority=osPriorityLow };
    worker = osThreadNew(TSKPTT_Log_Task, NULL, &attr);
    return worker != NULL;
}
/* Generated syscalls dispatch here. Never touch an emergency UART or consume a
 * protocol's RX bytes. Accepted output is queued; unavailable sinks return EOF. */
int __io_putchar(int value)
{
    if (!prepared || osKernelGetState() != osKernelRunning) return -1;
    const char ch=(char)value;
    return SRN_LogOutputLine(&ch,1u)==SRN_LOG_OUTPUT_OK ? (unsigned char)ch : -1;
}
int __io_getchar(void) { return -1; }
