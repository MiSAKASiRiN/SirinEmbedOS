#ifndef SRNOS_LWIP_CC_H
#define SRNOS_LWIP_CC_H
#include <stdint.h>
#include "SirinEmbedOS/Core/C/Fault.h"
#define LWIP_PLATFORM_ASSERT(message) SRNOS_Fault_Panic(SRNOS_FAULT_REASON_ASSERTION, (message), __FILE__, __func__, (uint32_t)__LINE__)
#define LWIP_PLATFORM_DIAG(message) ((void)0)
/* Fixed STM32H723 Arm little endian target; host tests also little endian. */
#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif
#endif
