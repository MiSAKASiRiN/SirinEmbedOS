#pragma once
#include "SirinEmbedOS/Services/Message.h"
#include "SirinEmbedOS/Services/SirinLink/SirinLink.h"
bool SRNOS_MessageFreeze(void);
bool SRNOS_MessageStart(void);
uint32_t SRNOS_MessageHeapBudget(void);
bool SRNOS_MessageTransportPrepare(const SRNOS_MessageTransportConfig *);
bool SRNOS_MessageTransportStart(void);
uint32_t SRNOS_MessageTransportHeapBudget(void);
bool SRNOS_MessageHasUART(void);
void SRNOS_MessagePollUART(void);
SRNOS_SirinLinkResult SRNOS_MessageRegisterHost(SRNOS_SirinLinkRegistration *);
SRNOS_MessageResult SRNOS_MessageReceiveWire(const SRNOS_MessageOrigin *,const uint8_t *,size_t,uint32_t);
SRNOS_MessageResult SRNOS_MessageReceiveVideo(const uint8_t *,size_t,uint32_t);
/* Frozen route metadata for HostComm registration, no runtime mutation. */
const SRNOS_MessageRouteConfig *SRNOS_MessageRouteAt(unsigned index);
