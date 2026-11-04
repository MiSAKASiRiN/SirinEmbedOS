#pragma once
#include "SirinEmbedOS/Services/Input.h"
bool SRNOS_InputFreeze(void);
bool SRNOS_InputStart(void);
uint32_t SRNOS_InputHeapBudget(void);
/* InputIO only: invalidate VT02 motion on UART stream discontinuity without
 * accepting a previously seen SirinRMT session/sequence again. */
void SRNOS_SirinRMTInvalidateVT02(uint16_t endpoint);
