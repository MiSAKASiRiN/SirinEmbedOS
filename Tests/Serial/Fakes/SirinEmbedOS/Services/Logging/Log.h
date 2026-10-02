#pragma once
/* System validation only needs backend bits, not the embedded logger backend. */
#define SRNOS_LOG_BACKEND_USB_CDC (1u << 0)
#define SRNOS_LOG_BACKEND_SWO (1u << 1)
#define SRNOS_LOG_BACKEND_UART (1u << 2)
