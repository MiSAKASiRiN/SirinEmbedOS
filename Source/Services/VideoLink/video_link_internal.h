#pragma once
#include "SirinEmbedOS/IO/Serial/Serial.h"
bool SRNOS_VideoLinkPrepare(SRNOS_SerialChannel *);
void SRNOS_VideoLinkFreeze(void);
void SRNOS_VideoLinkOnFrame(uint16_t command,const uint8_t *data,uint16_t length,uint8_t sequence,void *user);
void SRNOS_VideoLinkStep(uint32_t now_ms);
