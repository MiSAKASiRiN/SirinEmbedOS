#ifndef SRNOS_ETHERNET_INTERNAL_H
#define SRNOS_ETHERNET_INTERNAL_H
#include "SirinEmbedOS/IO/Ethernet/Ethernet.h"
bool SRNOS_EthernetPrepare(const SRNOS_EthernetConfig *config);
bool SRNOS_EthernetStart(void);
void SRNOS_EthernetWakeFromISR(void);
bool SRNOS_EthernetClaimNetwork(void);
SRNOS_EthernetResult SRNOS_EthernetNetworkSend(const uint8_t *frame, uint16_t length, uint32_t epoch);
SRNOS_EthernetResult SRNOS_EthernetNetworkReceive(SRNOS_EthernetFrame *frame);
#endif
