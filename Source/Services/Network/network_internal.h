#ifndef SRNOS_NETWORK_INTERNAL_H
#define SRNOS_NETWORK_INTERNAL_H
#include "SirinEmbedOS/Services/Network.h"
#define SRNOS_NETWORK_STACK_BYTES 6144u
bool SRNOS_NetworkPrepare(const SRNOS_NetworkConfig *config);
bool SRNOS_NetworkStart(void);
uint32_t SRNOS_NetworkHeapBudget(const SRNOS_NetworkConfig *config);
/* One fixed system datagram listener, bound once before Prepare freezes it.
 * Borrowed datagram; Network worker, outside SDK locks, bounded/nonblocking.
 * Returning false counts the datagram as dropped. No pointer may be retained. */
typedef bool (*SRNOS_NetworkDatagramHandler)(void *user, uint16_t local_port,
    const SRNOS_UDPDatagram *datagram);
bool SRNOS_NetworkBindDatagramHandler(uint16_t port,
    SRNOS_NetworkDatagramHandler handler, void *user);
bool SRNOS_NetworkDestinationValid(const SRNOS_NetworkConfig *config, const uint8_t ip[4]);
#endif
