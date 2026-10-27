#ifndef SRNOS_NETWORK_NTP_H
#define SRNOS_NETWORK_NTP_H
#include "SirinEmbedOS/Services/Network.h"
struct netif;
/* Solely owned by the existing NO_SYS Network worker. */
bool SRNOS_NTPInitialize(const SRNOS_NTPConfig *config,struct netif *interface);
void SRNOS_NTPStep(bool link_up,uint32_t link_epoch);
void SRNOS_NTPGetStatus(SRNOS_NTPStatus *status);
#endif
