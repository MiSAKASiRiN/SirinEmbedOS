#pragma once
#include "SirinEmbedOS/IO/Serial/Serial.h"
#ifdef __cplusplus
extern "C" {
#endif
/* NONE config disables Referee; binding freezes at Configure. */
int SRNOS_RefereeConfigure(SRNOS_SerialPortId port);
int SRNOS_RefereeStart(void);
#ifdef __cplusplus
}
#endif
