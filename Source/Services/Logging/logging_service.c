#include "thread_internal.h"
#include "SirinEmbedOS/Services/Services.h"
#include "Services/Logging/Worker/log_worker_internal.h"
#include "usb_cdc_log_transport.h"
static bool prepared;
static osThreadId_t worker;
static SRNOS_LogConfig saved_config;
bool SRNOS_LoggingPrepare(uint32_t mask, SRNOS_SerialChannel *uart)
{
    if (prepared || osKernelGetState() == osKernelRunning) return false;
    if (((mask & SRNOS_LOG_BACKEND_UART) != 0u) != (uart != NULL)) return false;
    if (!SRNOS_LogBindUart(uart)) return false;
    const SRNOS_LogUSBTransport usb = { NULL, SRNOS_USBCdcLogSubmit, SRNOS_USBCdcLogService };
    if (mask & SRNOS_LOG_BACKEND_USB_CDC) {
        SRNOS_LogRegisterUsbTransport(&usb);
    }
    saved_config = SRNOS_LOG_DEFAULT_CONFIG;
    saved_config.backend_mask = mask;
    SRNOS_LogSetBackendMask(mask);
    SRNOS_LogFreezeTransports();
    prepared = true;
    return true;
}
bool SRNOS_LoggingStart(void)
{
    if (!prepared || worker != NULL) return false;
    if (!SRNOS_LogInitResources(&saved_config)) return false;
    const osThreadAttr_t attr = { .name="SRNOS_Log", .stack_size=2048u, .priority=osPriorityLow };
    worker = SRNOS_KernelThreadNew(SRNOS_LogTask, NULL, &attr);
    return worker != NULL;
}
/* Generated syscalls dispatch here. Never touch an emergency UART or consume a
 * protocol's RX bytes. Accepted output is queued; unavailable sinks return EOF. */
int __io_putchar(int value)
{
    if (!prepared || osKernelGetState() != osKernelRunning) return -1;
    const char ch=(char)value;
    return SRNOS_LogOutputLine(&ch,1u)==SRNOS_LOG_OUTPUT_OK ? (unsigned char)ch : -1;
}
int __io_getchar(void) { return -1; }
