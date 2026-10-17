#include "network_internal.h"
static uint32_t address_value(const uint8_t a[4])
{ return (uint32_t)a[0]<<24 | (uint32_t)a[1]<<16 | (uint32_t)a[2]<<8 | a[3]; }
static bool unicast(uint32_t ip)
{ return (ip >> 24) != 0 && (ip >> 24) != 127 && (ip >> 24) < 224; }
bool SRNOS_NetworkDestinationValid(const SRNOS_NetworkConfig *c, const uint8_t ip[4])
{
    if (!c || !ip) return false;
    uint32_t a=address_value(ip), mask=address_value(c->netmask), own=address_value(c->address);
    if (!unicast(a) || a==own) return false;
    if ((a & mask)==(own & mask)) return (a & ~mask)!=0 && (a & ~mask)!=~mask;
    return address_value(c->gateway)!=0;
}
bool SRNOS_NetworkConfigValid(const SRNOS_NetworkConfig *c)
{
    if (!c || c->udp_port_count > SRNOS_UDP_PORT_LIMIT) return false;
    if (!c->enabled) return true;
    uint32_t ip=address_value(c->address), mask=address_value(c->netmask), host=~mask;
    uint32_t gateway=address_value(c->gateway);
    /* Conventional /1../30 subnets only, with host and broadcast addresses. */
    if (!unicast(ip) || !mask || host<3 || (host & (host+1)) ||
        !(ip & host) || (ip & host)==host) return false;
    if (gateway && (!unicast(gateway) || gateway==ip || (gateway & mask)!=(ip & mask) ||
        !(gateway & host) || (gateway & host)==host)) return false;
    for (unsigned i=0;i<c->udp_port_count;++i) {
        if (!c->udp_ports[i]) return false;
        for (unsigned j=0;j<i;++j) if (c->udp_ports[i]==c->udp_ports[j]) return false;
    }
    return true;
}
