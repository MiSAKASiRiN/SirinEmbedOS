#include "network_ntp.h"
#include "../Clock/clock_internal.h"
#include "SirinEmbedOS/Kernel/Thread.h"
#include "lwip/udp.h"
#include "lwip/etharp.h"
#include <string.h>

/* SNTP client/server exchange, RFC 4330 / RFC 5905. No NTP server, broadcast,
 * peer selection or clock-frequency discipline. All timestamps are software
 * timestamps; TX Ethernet queue and RX worker latency remain in measured RTT. */
#define SRNOS_NTP_PACKET_BYTES 48u
#define SRNOS_NTP_TIMEOUT_US UINT64_C(1000000)
#define SRNOS_NTP_RETRY_US UINT64_C(15000000)
#define SRNOS_NTP_MAX_RETRY_US UINT64_C(1024000000)
#define SRNOS_NTP_MAX_RTT_US UINT64_C(50000)
#define SRNOS_NTP_UNIX_OFFSET UINT64_C(2208988800)
#define SRNOS_NTP_UNIX_MIN UINT64_C(946684800)  /* 2000-01-01 */
#define SRNOS_NTP_UNIX_MAX UINT64_C(4102444800) /* 2100-01-01, exclusive */

static struct {
    SRNOS_NTPConfig config;
    SRNOS_NTPStatus status;
    struct udp_pcb *pcb;
    struct netif *interface;
    ip_addr_t server;
    uint64_t sent_us,next_us,retry_us;
    uint32_t link_epoch;
    uint8_t transmit_stamp[8];
    bool link_up,done,rate_limited;
} ntp;

static uint32_t ntp_read32(const uint8_t *p)
{ return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static void ntp_write32(uint8_t *p,uint32_t n)
{ p[0]=(uint8_t)(n>>24);p[1]=(uint8_t)(n>>16);p[2]=(uint8_t)(n>>8);p[3]=(uint8_t)n; }
static void ntp_encode(uint8_t *p,uint64_t unix_us)
{
    ntp_write32(p,(uint32_t)(unix_us/1000000u+SRNOS_NTP_UNIX_OFFSET));
    ntp_write32(p+4,(uint32_t)(((unix_us%1000000u)<<32)/1000000u));
}
static bool ntp_decode(const uint8_t *p,uint64_t *unix_us)
{
    uint64_t seconds=ntp_read32(p);
    uint32_t fraction=ntp_read32(p+4);
    if (!seconds && !fraction) return false;
    /* Unfold the 2036 NTP era rollover into the documented 2000..2099 window. */
    if (seconds<SRNOS_NTP_UNIX_OFFSET) seconds+=UINT64_C(1)<<32;
    seconds-=SRNOS_NTP_UNIX_OFFSET;
    if (seconds<SRNOS_NTP_UNIX_MIN || seconds>=SRNOS_NTP_UNIX_MAX) return false;
    *unix_us=seconds*1000000u+(((uint64_t)fraction*1000000u)>>32);
    return true;
}
static void ntp_backoff(uint64_t now)
{
    ntp.status.pending=false;
    ntp.next_us=now+ntp.retry_us;
    if (ntp.retry_us<SRNOS_NTP_MAX_RETRY_US/2) ntp.retry_us*=2;
    else ntp.retry_us=SRNOS_NTP_MAX_RETRY_US;
    SRNOS_ClockHoldover();
}
static void ntp_receive(void *arg,struct udp_pcb *pcb,struct pbuf *p,
    const ip_addr_t *address,u16_t port)
{
    (void)arg;(void)pcb;
    const uint64_t now=SRNOS_TimeUs();
    uint8_t packet[SRNOS_NTP_PACKET_BYTES];
    bool valid=ntp.status.pending && ntp.link_up && port==123 &&
        ip_addr_eq(address,&ntp.server) && p->tot_len==sizeof(packet) &&
        now>=ntp.sent_us && now-ntp.sent_us<=SRNOS_NTP_TIMEOUT_US;
    if (valid) valid=pbuf_copy_partial(p,packet,sizeof(packet),0)==sizeof(packet);
    pbuf_free(p);
    if (!valid) { ++ntp.status.rejected;return; }
    unsigned version=(packet[0]>>3)&7u,mode=packet[0]&7u,leap=packet[0]>>6;
    if ((version!=3 && version!=4) || mode!=4 ||
        memcmp(packet+24,ntp.transmit_stamp,8)) { ++ntp.status.rejected;return; }
    if (packet[1]==0) {
        ++ntp.status.kiss_of_death;
        if (!memcmp(packet+12,"DENY",4) || !memcmp(packet+12,"RSTR",4)) ntp.status.denied=true;
        if (!memcmp(packet+12,"RATE",4)) {
            ntp.rate_limited=true;
            unsigned exponent=packet[2]<6?6:packet[2];
            if (exponent>10) exponent=10;
            uint64_t delay=(UINT64_C(1)<<exponent)*1000000u;
            if (ntp.retry_us<delay) ntp.retry_us=delay;
        }
        ntp_backoff(now);return;
    }
    uint64_t server_rx,server_tx;
    if (leap==3 || packet[1]>15 || !ntp_decode(packet+32,&server_rx) ||
        !ntp_decode(packet+40,&server_tx) || server_tx<server_rx) {
        ++ntp.status.rejected;return;
    }
    const uint64_t elapsed=now-ntp.sent_us,processing=server_tx-server_rx;
    /* Permit only two microseconds of fixed-point rounding discrepancy. */
    if (processing>elapsed+2u || (processing<=elapsed && elapsed-processing>SRNOS_NTP_MAX_RTT_US)) {
        ++ntp.status.rejected;return;
    }
    const uint32_t rtt=processing>elapsed?0u:(uint32_t)(elapsed-processing);
    /* Equivalent to four-timestamp offset calculation, including first sync
     * when the local clock has no Unix epoch yet. Assume symmetric path delay. */
    const uint64_t unix_now=server_tx+rtt/2u;
    if (unix_now>=SRNOS_NTP_UNIX_MAX*1000000u) { ++ntp.status.rejected;return; }
    uint64_t fresh=ntp.config.poll_interval_ms?
        (uint64_t)ntp.config.poll_interval_ms*1000u+SRNOS_NTP_TIMEOUT_US:0;
    SRNOS_ClockSynchronize(now,unix_now,rtt,packet[1],(uint8_t)leap,fresh);
    ++ntp.status.accepted;ntp.status.pending=false;
    ntp.rate_limited=false;
    ntp.retry_us=SRNOS_NTP_RETRY_US;
    ntp.done=ntp.config.poll_interval_ms==0;
    ntp.next_us=now+(uint64_t)ntp.config.poll_interval_ms*1000u;
}
bool SRNOS_NTPInitialize(const SRNOS_NTPConfig *config,struct netif *interface)
{
    ntp.config=*config;ntp.interface=interface;ntp.retry_us=SRNOS_NTP_RETRY_US;
    if (!config->enabled) return true;
    IP_ADDR4(&ntp.server,config->server_address[0],config->server_address[1],
        config->server_address[2],config->server_address[3]);
    ntp.pcb=udp_new();
    /* Bind after all application ports: lwIP chooses an unused ephemeral port. */
    if (!ntp.pcb) return false;
    if (udp_bind(ntp.pcb,IP_ADDR_ANY,0)!=ERR_OK) { udp_remove(ntp.pcb);ntp.pcb=NULL;return false; }
    udp_recv(ntp.pcb,ntp_receive,NULL);
    return true;
}
void SRNOS_NTPStep(bool up,uint32_t epoch)
{
    if (!ntp.config.enabled) return;
    const uint64_t now=SRNOS_TimeUs();
    if (up!=ntp.link_up || epoch!=ntp.link_epoch) {
        ntp.link_up=up;ntp.link_epoch=epoch;ntp.status.pending=false;
        /* Link cycling must not bypass a server's RATE backoff. */
        if (!ntp.rate_limited || now>=ntp.next_us) ntp.next_us=now;
        SRNOS_ClockHoldover();
    }
    if (!up || ntp.done || ntp.status.denied) return;
    if (ntp.status.pending) {
        if (now-ntp.sent_us>SRNOS_NTP_TIMEOUT_US) { ++ntp.status.timeouts;ntp_backoff(now); }
        return;
    }
    if (now<ntp.next_us) return;
    ntp.rate_limited=false;
    const ip4_addr_t *neighbor=ip_2_ip4(&ntp.server);
    if (!ip4_addr_netcmp(neighbor,netif_ip4_addr(ntp.interface),netif_ip4_netmask(ntp.interface)))
        neighbor=netif_ip4_gw(ntp.interface);
    struct eth_addr *mac;const ip4_addr_t *resolved;
    if (etharp_find_addr(ntp.interface,neighbor,&mac,&resolved)<0) {
        (void)etharp_request(ntp.interface,neighbor);
        ntp.next_us=now+1000000u;return;
    }
    struct pbuf *p=pbuf_alloc(PBUF_TRANSPORT,SRNOS_NTP_PACKET_BYTES,PBUF_RAM);
    if (!p) { ++ntp.status.send_errors;ntp_backoff(now);return; }
    uint8_t packet[SRNOS_NTP_PACKET_BYTES]={0};
    packet[0]=(4u<<3)|3u;packet[2]=6;packet[3]=(uint8_t)-20;
    SRNOS_ClockSnapshot clock;
    (void)SRNOS_ClockGetSnapshot(&clock);
    ntp.sent_us=clock.uptime_us;
    ntp_encode(packet+40,clock.absolute_valid?clock.unix_us:clock.uptime_us);
    memcpy(ntp.transmit_stamp,packet+40,8);
    pbuf_take(p,packet,sizeof(packet));
    if (udp_sendto(ntp.pcb,p,&ntp.server,123)==ERR_OK) {
        ntp.status.pending=true;++ntp.status.requests;
    } else { ++ntp.status.send_errors;ntp_backoff(now); }
    pbuf_free(p);
}
void SRNOS_NTPGetStatus(SRNOS_NTPStatus *status) { *status=ntp.status; }
