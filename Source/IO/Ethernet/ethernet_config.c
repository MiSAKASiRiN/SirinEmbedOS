#include "SirinEmbedOS/IO/Ethernet/Ethernet.h"
bool SRNOS_EthernetConfigValid(const SRNOS_EthernetConfig *config)
{
    if (!config) return false;
    unsigned bits = 0;
    for (unsigned i = 0; i < 6; ++i) bits |= config->mac[i];
    return bits == 0 || (config->mac[0] & 1u) == 0;
}
