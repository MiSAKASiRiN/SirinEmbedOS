#ifndef SRNOS_NETWORK_INTERNAL_H
#define SRNOS_NETWORK_INTERNAL_H
#include "SirinEmbedOS/Services/Network.h"
bool SRNOS_NetworkPrepare(const SRNOS_NetworkConfig *config);
bool SRNOS_NetworkStart(void);
bool SRNOS_NetworkBindMessages(uint16_t port);
bool SRNOS_NetworkDestinationValid(const SRNOS_NetworkConfig *config, const uint8_t ip[4]);
#endif
