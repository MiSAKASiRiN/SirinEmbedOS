#include "network_internal.h"
#include "network_ntp.h"
#include "IO/Ethernet/ethernet_internal.h"
#include "Kernel/thread_internal.h"
#include "queue.h"
#include "lwip/init.h"
#include "lwip/udp.h"
#include "lwip/etharp.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"
#include <string.h>

typedef struct {
    uint32_t epoch, queued_ms;
    uint8_t destination[4], port_index;
    uint16_t port, length;
    uint8_t data[SRNOS_UDP_PAYLOAD_MAX];
} tx_item_t;
typedef struct { uint32_t epoch; SRNOS_UDPDatagram datagram; } rx_item_t;
static SRNOS_NetworkConfig configuration;
static SRNOS_NetworkStatus current, published;
static uint32_t accepted;
static bool prepared, initialized, pending;
static uint32_t next_arp;
static struct netif interface;
static struct udp_pcb *ports[SRNOS_UDP_PORT_LIMIT+1];
static struct {
    uint16_t port;
    SRNOS_NetworkDatagramHandler handler;
    void *user;
} listener;

uint32_t SRNOS_NetworkHeapBudget(const SRNOS_NetworkConfig *config)
{
    /* Queue storage and lwIP buffers are static; reserve the worker stack plus
     * its TCB/TLS and allocator overhead, preserving the original 7 KiB total. */
    return config && config->enabled ? SRNOS_NETWORK_STACK_BYTES + 1024u : 0u;
}

bool SRNOS_NetworkBindDatagramHandler(uint16_t port,
    SRNOS_NetworkDatagramHandler handler, void *user)
{
    if (prepared || listener.port || !port || !handler || xPortIsInsideInterrupt() ||
        xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) return false;
    listener.port = port;
    listener.handler = handler;
    listener.user = user;
    return true;
}
static QueueHandle_t tx_queue, rx_queue[SRNOS_UDP_PORT_LIMIT];
static StaticQueue_t tx_control, rx_control[SRNOS_UDP_PORT_LIMIT];
static uint8_t tx_storage[SRNOS_UDP_TX_DEPTH * sizeof(tx_item_t)];
static uint8_t rx_storage[SRNOS_UDP_PORT_LIMIT][sizeof(rx_item_t)];
static bool rx_latched[SRNOS_UDP_PORT_LIMIT];
static TaskHandle_t worker;
/* These scratch buffers are owned exclusively by the Network worker. */
static tx_item_t tx_item;
static SRNOS_EthernetFrame rx_frame;
static uint8_t tx_frame[SRNOS_ETHERNET_FRAME_MAX];
/* Selector snapshot: worker-owned, avoids another 1.5 KiB on its stack. */
static rx_item_t rx_previous;

/* lwIP NO_SYS port ABI: called exclusively by the Network worker. */
u32_t sys_now(void) { return SRNOS_TimeMs(); }
static void publish(void)
{
    SRNOS_NTPGetStatus(&current.ntp);
    taskENTER_CRITICAL();
    published = current;
    taskEXIT_CRITICAL();
}
bool SRNOS_NetworkGetStatus(SRNOS_NetworkStatus *status)
{
    if (!status || !SRNOS_KernelTaskContext()) return false;
    taskENTER_CRITICAL();
    *status = published;
    status->udp_accepted = accepted;
    taskEXIT_CRITICAL();
    return true;
}
static int port_index(uint16_t port)
{
    if(listener.port&&port==listener.port)return SRNOS_UDP_PORT_LIMIT;
    for (unsigned i=0;i<configuration.udp_port_count;++i)
        if (configuration.udp_ports[i]==port) return (int)i;
    return -1;
}
static bool live_status(SRNOS_NetworkStatus *status)
{
    SRNOS_EthernetStatus eth;
    return SRNOS_NetworkGetStatus(status) && status->ready && status->link_up &&
        SRNOS_EthernetGetStatus(&eth) && eth.state==SRNOS_ETHERNET_LINK_UP &&
        eth.link_epoch==status->link_epoch;
}
SRNOS_NetworkResult SRNOS_UDPSend(uint16_t local_port, const uint8_t destination[4],
    uint16_t destination_port, const void *data, uint16_t length)
{
    if (!SRNOS_KernelTaskContext()) return SRNOS_NETWORK_CONTEXT;
    int index=port_index(local_port);
    if (index<0 || !destination_port || (!data && length) || length>SRNOS_UDP_PAYLOAD_MAX ||
        !SRNOS_NetworkDestinationValid(&configuration,destination)) return SRNOS_NETWORK_INVALID;
    SRNOS_NetworkStatus status;
    if (!live_status(&status)) return SRNOS_NETWORK_NOT_READY;
    tx_item_t item = {.epoch=status.link_epoch, .queued_ms=SRNOS_TimeMs(),
        .port_index=(uint8_t)index, .port=destination_port, .length=length};
    memcpy(item.destination,destination,4);
    if (length) memcpy(item.data,data,length);
    if (xQueueSend(tx_queue,&item,0)!=pdPASS) return SRNOS_NETWORK_FULL;
    taskENTER_CRITICAL(); ++accepted; taskEXIT_CRITICAL();
    return SRNOS_NETWORK_OK;
}
SRNOS_NetworkResult SRNOS_UDPReceive(uint16_t local_port, SRNOS_UDPDatagram *datagram)
{
    if (!SRNOS_KernelTaskContext()) return SRNOS_NETWORK_CONTEXT;
    int index=port_index(local_port);
    if (index<0 || (unsigned)index>=SRNOS_UDP_PORT_LIMIT || !datagram) return SRNOS_NETWORK_INVALID;
    SRNOS_NetworkStatus status;
    const bool live=live_status(&status);
    rx_item_t item;
    /* Consume the slot and latch as one operation. No callback runs here. */
    taskENTER_CRITICAL();
    bool latched=rx_latched[index];
    if (!live && !latched) { taskEXIT_CRITICAL(); return SRNOS_NETWORK_NOT_READY; }
    bool received=rx_queue[index] && xQueueReceive(rx_queue[index],&item,0)==pdPASS;
    if (received) rx_latched[index]=false;
    taskEXIT_CRITICAL();
    if (!received) return SRNOS_NETWORK_EMPTY;
    if (!latched && (item.epoch!=status.link_epoch ||
        (uint32_t)(SRNOS_TimeMs()-item.datagram.received_ms)>SRNOS_UDP_LIFETIME_MS))
        return SRNOS_NETWORK_EMPTY;
    *datagram=item.datagram;
    return SRNOS_NETWORK_OK;
}
static err_t link_output(struct netif *netif, struct pbuf *p)
{
    (void)netif;
    if (p->tot_len>sizeof(tx_frame) || pbuf_copy_partial(p,tx_frame,p->tot_len,0)!=p->tot_len)
        return ERR_BUF;
    if (SRNOS_EthernetNetworkSend(tx_frame,p->tot_len,current.link_epoch)!=SRNOS_ETHERNET_OK) {
        ++current.tx_errors;
        return ERR_IF;
    }
    ++current.tx_frames;
    return ERR_OK;
}
static err_t interface_init(struct netif *netif)
{
    netif->name[0]='e'; netif->name[1]='0';
    netif->mtu=1500;
    netif->hwaddr_len=6;
    netif->flags=NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    netif->output=etharp_output;
    netif->linkoutput=link_output;
    return ERR_OK;
}
static void udp_received(void *argument, struct udp_pcb *pcb, struct pbuf *p,
    const ip_addr_t *address, u16_t port)
{
    (void)pcb;
    unsigned index=(unsigned)(uintptr_t)argument;
    rx_item_t item={.epoch=current.link_epoch};
    memcpy(item.datagram.source_address,&ip_2_ip4(address)->addr,4);
    if (p->tot_len>SRNOS_UDP_PAYLOAD_MAX || !port ||
        !SRNOS_NetworkDestinationValid(&configuration,item.datagram.source_address)) {
        ++current.udp_rx_dropped;
    } else {
        item.datagram.source_port=port;
        item.datagram.length=p->tot_len;
        item.datagram.received_ms=SRNOS_TimeMs();
        pbuf_copy_partial(p,item.datagram.data,p->tot_len,0);
        ++current.udp_received;
        if(index==SRNOS_UDP_PORT_LIMIT) {
            if(!listener.handler(listener.user,listener.port,&item.datagram))
                ++current.udp_rx_dropped;
            pbuf_free(p);return;
        }
        SRNOS_UDPReceiveAction action=SRNOS_UDP_RX_KEEP_FIRST;
        if (configuration.udp_rx_select[index]) {
            bool queued=xQueuePeek(rx_queue[index],&rx_previous,0)==pdPASS;
            action=configuration.udp_rx_select[index](&item.datagram,
                queued ? &rx_previous.datagram : NULL);
        }
        taskENTER_CRITICAL();
        if (action==SRNOS_UDP_RX_LATCH ||
            (action==SRNOS_UDP_RX_LATEST && !rx_latched[index])) {
            if (uxQueueMessagesWaiting(rx_queue[index])) ++current.udp_rx_replaced;
            SRNOS_ASSERT(xQueueOverwrite(rx_queue[index],&item)==pdPASS);
            rx_latched[index]=action==SRNOS_UDP_RX_LATCH;
            if (rx_latched[index]) ++current.udp_rx_latched;
        } else if (action!=SRNOS_UDP_RX_KEEP_FIRST || rx_latched[index] ||
                   xQueueSend(rx_queue[index],&item,0)!=pdPASS) {
            ++current.udp_rx_dropped;
        }
        taskEXIT_CRITICAL();
    }
    pbuf_free(p);
}
static void ip_address(ip4_addr_t *out, const uint8_t in[4])
{ IP4_ADDR(out,in[0],in[1],in[2],in[3]); }
static bool initialize(const SRNOS_EthernetStatus *eth)
{
    lwip_init();
    ip4_addr_t address, mask, gateway;
    ip_address(&address,configuration.address);
    ip_address(&mask,configuration.netmask);
    ip_address(&gateway,configuration.gateway);
    if (!netif_add(&interface,&address,&mask,&gateway,NULL,interface_init,ethernet_input)) return false;
    memcpy(interface.hwaddr,eth->mac,6);
    netif_set_default(&interface);
    for (unsigned i=0;i<configuration.udp_port_count;++i) {
        ports[i]=udp_new();
        if (!ports[i] || udp_bind(ports[i],IP_ADDR_ANY,configuration.udp_ports[i])!=ERR_OK) return false;
        udp_recv(ports[i],udp_received,(void *)(uintptr_t)i);
    }
    if(listener.port) {
        ports[SRNOS_UDP_PORT_LIMIT]=udp_new();
        if(!ports[SRNOS_UDP_PORT_LIMIT]||udp_bind(ports[SRNOS_UDP_PORT_LIMIT],IP_ADDR_ANY,listener.port)!=ERR_OK)return false;
        udp_recv(ports[SRNOS_UDP_PORT_LIMIT],udp_received,(void *)(uintptr_t)SRNOS_UDP_PORT_LIMIT);
    }
    return SRNOS_NTPInitialize(&configuration.ntp,&interface);
}
static void flush(void)
{
    if (pending) { ++current.udp_tx_dropped; pending=false; }
    for (unsigned i=0;i<SRNOS_UDP_TX_DEPTH;++i)
        if (xQueueReceive(tx_queue,&tx_item,0)==pdPASS) ++current.udp_tx_dropped;
    rx_item_t item;
    for (unsigned i=0;i<configuration.udp_port_count;++i) {
        taskENTER_CRITICAL();
        if (!rx_latched[i] && xQueueReceive(rx_queue[i],&item,0)==pdPASS) ++current.udp_rx_dropped;
        taskEXIT_CRITICAL();
    }
}
static void receive_frames(void)
{
    for (unsigned i=0;i<4;++i) {
        if (SRNOS_EthernetNetworkReceive(&rx_frame)!=SRNOS_ETHERNET_OK) break;
        ++current.rx_frames;
        uint8_t *f=rx_frame.data;
        unsigned type=(unsigned)f[12]<<8 | f[13];
        /* Only ARP and unicast IPv4 UDP/ICMP. lwIP validates lengths, headers
         * and software checksums; disabled protocols do not emit responses. */
        if (type!=0x0806 && (type!=0x0800 || rx_frame.length<34 ||
            memcmp(f+30,configuration.address,4) || (f[23]!=1 && f[23]!=17))) {
            ++current.rx_dropped; continue;
        }
        struct pbuf *p=pbuf_alloc(PBUF_RAW,rx_frame.length,PBUF_RAM);
        if (!p) { ++current.rx_dropped; continue; }
        pbuf_take(p,f,rx_frame.length);
        if (interface.input(p,&interface)!=ERR_OK) { pbuf_free(p); ++current.rx_dropped; }
    }
}
static void send_datagram(void)
{
    if (!pending) {
        if (xQueueReceive(tx_queue,&tx_item,0)!=pdPASS) return;
        pending=true; next_arp=SRNOS_TimeMs();
    }
    uint32_t now=SRNOS_TimeMs();
    if (tx_item.epoch!=current.link_epoch || (uint32_t)(now-tx_item.queued_ms)>SRNOS_UDP_LIFETIME_MS) {
        pending=false; ++current.udp_tx_dropped; return;
    }
    ip_addr_t destination;
    ip_address(ip_2_ip4(&destination),tx_item.destination);
    const ip4_addr_t *neighbor=ip_2_ip4(&destination);
    if (!ip4_addr_netcmp(neighbor,netif_ip4_addr(&interface),netif_ip4_netmask(&interface)))
        neighbor=netif_ip4_gw(&interface);
    struct eth_addr *mac;
    const ip4_addr_t *resolved;
    if (etharp_find_addr(&interface,neighbor,&mac,&resolved)<0) {
        /* Resolve before udp_sendto: no application payload retained by lwIP's
         * longer ARP queue timeout. One pending item bounds head-of-line delay. */
        if ((int32_t)(now-next_arp)>=0) { (void)etharp_request(&interface,neighbor); next_arp=now+20u; }
        return;
    }
    struct pbuf *p=pbuf_alloc(PBUF_TRANSPORT,tx_item.length,PBUF_RAM);
    if (!p) { ++current.udp_tx_dropped; pending=false; return; }
    pbuf_take(p,tx_item.data,tx_item.length);
    if (udp_sendto(ports[tx_item.port_index],p,&destination,tx_item.port)==ERR_OK) ++current.udp_submitted;
    else ++current.udp_tx_dropped;
    pbuf_free(p);
    pending=false;
}
static void step(void *context)
{
    (void)context;
    SRNOS_EthernetStatus eth;
    if (!SRNOS_EthernetGetStatus(&eth)) return;
    bool up=eth.state==SRNOS_ETHERNET_LINK_UP;
    if (!initialized) {
        if (!up) return;
        SRNOS_ASSERT(initialize(&eth));
        initialized=true;
        current.ready=true;
    }
    if (eth.link_epoch!=current.link_epoch || up!=current.link_up) {
        current.link_up=false;
        publish(); /* Block new sends before dropping old epoch queues. */
        netif_set_down(&interface);
        netif_set_link_down(&interface);
        etharp_cleanup_netif(&interface);
        flush();
        current.link_epoch=eth.link_epoch;
        current.link_up=up;
        if (up) { netif_set_link_up(&interface); netif_set_up(&interface); }
    }
    SRNOS_NTPStep(up,current.link_epoch);
    if (up) { receive_frames(); send_datagram(); }
    sys_check_timeouts();
    publish();
}
static bool wait_next(void *context)
{
    (void)context;
    vTaskDelay(pdMS_TO_TICKS(5)); /* RunLoop period is metrics only, not a delay. */
    return true;
}
static void entry(void *context)
{
    (void)context;
    const SRNOS_InternalLoop loop={step,wait_next,NULL,0,false};
    SRNOS_KernelRunLoop(&loop);
}
bool SRNOS_NetworkPrepare(const SRNOS_NetworkConfig *config)
{
    if (prepared || !SRNOS_NetworkConfigValid(config)) return false;
    if(listener.port) {
        if(!config->enabled)return false;
        for(unsigned i=0;i<config->udp_port_count;i++)if(config->udp_ports[i]==listener.port)return false;
    }
    if (config->enabled && !SRNOS_EthernetClaimNetwork()) return false;
    configuration=*config;
    current.enabled=config->enabled;
    memcpy(current.address,config->address,4);
    memcpy(current.netmask,config->netmask,4);
    memcpy(current.gateway,config->gateway,4);
    published=current;
    prepared=true;
    return true;
}
bool SRNOS_NetworkStart(void)
{
    if (!prepared) return false;
    if (!configuration.enabled) return true;
    if (worker) return false;
    tx_queue=xQueueCreateStatic(SRNOS_UDP_TX_DEPTH,sizeof(tx_item_t),tx_storage,&tx_control);
    if (!tx_queue) return false;
    for (unsigned i=0;i<configuration.udp_port_count;++i) {
        rx_queue[i]=xQueueCreateStatic(1,sizeof(rx_item_t),rx_storage[i],&rx_control[i]);
        if (!rx_queue[i]) return false;
    }
    const osThreadAttr_t attributes={.name="SRNOS_Network",.stack_size=SRNOS_NETWORK_STACK_BYTES,.priority=15};
    worker=(TaskHandle_t)SRNOS_KernelThreadNew(entry,NULL,&attributes);
    return worker!=NULL;
}
