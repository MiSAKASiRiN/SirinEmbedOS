#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Services/Network/network.c"

static uint32_t clock_ms=1;
static bool task_context=true, claimed;
static SRNOS_EthernetStatus ethernet={.state=SRNOS_ETHERNET_LINK_UP,.mac={2,1,2,3,4,5},.link_epoch=1};
static SRNOS_EthernetFrame incoming, outgoing[32];
static bool have_input;
static unsigned sent;
static const uint8_t host_mac[6]={2,6,7,8,9,10};
static uint8_t host_ip[4]={192,168,1,100};
static osThreadFunc_t start_entry;
static bool consume_on_peek;
static SRNOS_UDPDatagram preempted_datagram;
uint32_t SRNOS_TimeMs(void) { return clock_ms; }
bool SRNOS_KernelTaskContext(void) { return task_context; }
osThreadId_t SRNOS_KernelThreadNew(osThreadFunc_t fn,void *arg,const osThreadAttr_t *attr)
{ (void)arg; assert(attr->priority<16 && attr->stack_size==6144); start_entry=fn; return (void *)1; }
void vTaskDelay(uint32_t ticks) { assert(ticks>=5); clock_ms+=ticks; }
void SRNOS_KernelRunLoop(const SRNOS_InternalLoop *loop)
{ assert(loop->wait);loop->step(NULL);uint32_t before=clock_ms;assert(loop->wait(NULL));assert(clock_ms-before>=5); }
QueueHandle_t xQueueCreateStatic(unsigned n,unsigned size,uint8_t *storage,StaticQueue_t *q)
{ *q=(StaticQueue_t){.capacity=n,.size=size,.storage=storage}; return q; }
int xQueueSend(QueueHandle_t q,const void *item,unsigned wait)
{
    assert(!wait); if(q->count==q->capacity)return 0;
    memcpy(q->storage+((q->head+q->count)%q->capacity)*q->size,item,q->size);++q->count;return 1;
}
int xQueueReceive(QueueHandle_t q,void *item,unsigned wait)
{
    assert(!wait);if(!q->count)return 0;
    memcpy(item,q->storage+q->head*q->size,q->size);q->head=(q->head+1)%q->capacity;--q->count;return 1;
}
int xQueuePeek(QueueHandle_t q,void *item,unsigned wait)
{
    assert(!wait);if(!q->count)return 0;
    memcpy(item,q->storage+q->head*q->size,q->size);
    if(consume_on_peek) {
        consume_on_peek=false;
        assert(SRNOS_UDPReceive(5000,&preempted_datagram)==SRNOS_NETWORK_OK);
    }
    return 1;
}
int xQueueOverwrite(QueueHandle_t q,const void *item)
{ assert(q->capacity==1);memcpy(q->storage,item,q->size);q->head=0;q->count=1;return 1; }
unsigned uxQueueMessagesWaiting(QueueHandle_t q) { return q->count; }
bool SRNOS_EthernetClaimNetwork(void) { assert(!claimed);claimed=true;return true; }
bool SRNOS_EthernetGetStatus(SRNOS_EthernetStatus *status) { *status=ethernet;return true; }
SRNOS_EthernetResult SRNOS_EthernetNetworkSend(const uint8_t *frame,uint16_t length,uint32_t epoch)
{
    assert(epoch==ethernet.link_epoch);
    assert(length<=1514 && length>=14 && sent<32);
    outgoing[sent].length=length;memcpy(outgoing[sent++].data,frame,length);return SRNOS_ETHERNET_OK;
}
SRNOS_EthernetResult SRNOS_EthernetNetworkReceive(SRNOS_EthernetFrame *frame)
{ if(!have_input)return SRNOS_ETHERNET_EMPTY;*frame=incoming;have_input=false;return SRNOS_ETHERNET_OK; }
void SRNOS_Fault_Panic(SRNOS_Fault_Reason reason,const char *expression,const char *file,const char *function,uint32_t line)
{ (void)reason;fprintf(stderr,"panic %s %s:%u %s\n",expression,file,line,function);abort(); }
static void put16(uint8_t *p,unsigned value) { p[0]=value>>8;p[1]=value; }
static unsigned sum_bytes(const uint8_t *p,unsigned n,unsigned sum)
{ for(unsigned i=0;i<n;i+=2)sum+=(unsigned)p[i]<<8 | (i+1<n?p[i+1]:0);return sum; }
static unsigned checksum(const uint8_t *p,unsigned n,unsigned initial)
{ unsigned s=sum_bytes(p,n,initial);while(s>>16)s=(s&65535)+(s>>16);return (~s)&65535; }
static void feed(void) { have_input=true;step(NULL); }
static void arp(unsigned operation)
{
    uint8_t *p=incoming.data;memset(p,0,60);incoming.length=60;
    memcpy(p,ethernet.mac,6);memcpy(p+6,host_mac,6);put16(p+12,0x806);
    put16(p+14,1);put16(p+16,0x800);p[18]=6;p[19]=4;put16(p+20,operation);
    memcpy(p+22,host_mac,6);memcpy(p+28,host_ip,4);memcpy(p+32,ethernet.mac,6);
    memcpy(p+38,configuration.address,4);
}
static void ip_frame(unsigned protocol,unsigned payload_length)
{
    uint8_t *p=incoming.data;memset(p,0,sizeof(incoming.data));
    incoming.length=14+20+8+payload_length;
    memcpy(p,ethernet.mac,6);memcpy(p+6,host_mac,6);put16(p+12,0x800);
    p[14]=0x45;put16(p+16,20+8+payload_length);p[22]=64;p[23]=protocol;
    memcpy(p+26,host_ip,4);memcpy(p+30,configuration.address,4);
    put16(p+24,checksum(p+14,20,0));
    for(unsigned i=0;i<payload_length;++i)p[42+i]=(uint8_t)(i*17);
    if(protocol==17) {
        put16(p+34,40000);put16(p+36,5000);put16(p+38,8+payload_length);
        unsigned pseudo=sum_bytes(p+26,8,17+8+payload_length);
        unsigned check=checksum(p+34,8+payload_length,pseudo);
        put16(p+40,check?check:65535);
    } else { p[34]=8;put16(p+38,0x1234);put16(p+40,1);put16(p+36,checksum(p+34,8+payload_length,0)); }
}
static void verify_ipv4(const SRNOS_EthernetFrame *frame,unsigned proto)
{
    const uint8_t *p=frame->data;assert(p[12]==8 && p[13]==0 && p[23]==proto);
    assert(checksum(p+14,20,0)==0 && !memcmp(p+30,host_ip,4));
    unsigned size=((unsigned)p[16]<<8|p[17])-20;
    unsigned pseudo=proto==17?sum_bytes(p+26,8,proto+size):0;
    assert(checksum(p+34,size,pseudo)==0);
}
static unsigned selector_calls;
static SRNOS_UDPReceiveAction select_packet(const SRNOS_UDPDatagram *next,const SRNOS_UDPDatagram *queued)
{
    ++selector_calls;
    assert(next->length==2 && (!queued || queued->length==2));
    return (SRNOS_UDPReceiveAction)next->data[0];
}
static void policy_packet(unsigned action,unsigned value)
{
    ip_frame(17,2);
    uint8_t *p=incoming.data;p[42]=action;p[43]=value;p[40]=p[41]=0;
    unsigned check=checksum(p+34,10,sum_bytes(p+26,8,17+10));
    put16(p+40,check?check:65535);feed();
}
static void test_policy(void)
{
    SRNOS_UDPDatagram d;
    policy_packet(SRNOS_UDP_RX_KEEP_FIRST,1);policy_packet(SRNOS_UDP_RX_KEEP_FIRST,2);
    assert(SRNOS_UDPReceive(5000,&d)==0 && d.data[1]==1);
    policy_packet(SRNOS_UDP_RX_LATEST,3);policy_packet(SRNOS_UDP_RX_LATEST,4);
    assert(SRNOS_UDPReceive(5000,&d)==0 && d.data[1]==4);
    policy_packet(SRNOS_UDP_RX_LATEST,5);policy_packet(SRNOS_UDP_RX_LATCH,6);
    for(unsigned i=0;i<100;++i)policy_packet(SRNOS_UDP_RX_LATEST,7);
    policy_packet(SRNOS_UDP_RX_KEEP_FIRST,8);policy_packet(SRNOS_UDP_RX_DROP,9);
    policy_packet(255,10);
    assert(SRNOS_UDPReceive(5000,&d)==0 && d.data[1]==6);
    assert(SRNOS_UDPReceive(5000,&d)==SRNOS_NETWORK_EMPTY);
    policy_packet(SRNOS_UDP_RX_LATEST,11);
    assert(SRNOS_UDPReceive(5000,&d)==0 && d.data[1]==11);
    policy_packet(SRNOS_UDP_RX_LATCH,12);policy_packet(SRNOS_UDP_RX_LATCH,13);
    uint32_t received_ms=clock_ms;
    ++ethernet.link_epoch;ethernet.state=SRNOS_ETHERNET_LINK_DOWN;step(NULL);
    clock_ms+=10000;
    task_context=false;assert(SRNOS_UDPReceive(5000,&d)==SRNOS_NETWORK_CONTEXT);task_context=true;
    assert(SRNOS_UDPReceive(5000,&d)==0 && d.data[1]==13 && d.received_ms==received_ms);
    assert(SRNOS_UDPReceive(5000,&d)==SRNOS_NETWORK_NOT_READY);
    ethernet.state=SRNOS_ETHERNET_LINK_UP;++ethernet.link_epoch;step(NULL);
    policy_packet(SRNOS_UDP_RX_LATEST,14);++ethernet.link_epoch;step(NULL);
    assert(SRNOS_UDPReceive(5000,&d)==SRNOS_NETWORK_EMPTY);
    policy_packet(SRNOS_UDP_RX_LATEST,15);clock_ms+=101;
    assert(SRNOS_UDPReceive(5000,&d)==SRNOS_NETWORK_EMPTY);
    /* A higher priority consumer may run after peek, before selector/commit. */
    policy_packet(SRNOS_UDP_RX_LATEST,16);consume_on_peek=true;
    policy_packet(SRNOS_UDP_RX_LATCH,17);
    assert(!consume_on_peek && preempted_datagram.data[1]==16);
    assert(SRNOS_UDPReceive(5000,&d)==0 && d.data[1]==17);
    policy_packet(SRNOS_UDP_RX_LATCH,18);consume_on_peek=true;
    policy_packet(SRNOS_UDP_RX_LATEST,19);
    assert(!consume_on_peek && preempted_datagram.data[1]==18);
    assert(SRNOS_UDPReceive(5000,&d)==0 && d.data[1]==19);
    SRNOS_NetworkStatus status;assert(SRNOS_NetworkGetStatus(&status));
    assert(selector_calls>100 && status.udp_rx_latched==5 && status.udp_rx_replaced==3);
    puts("PASS: opt-in latest/latch, burst protection, coalescing, age/link retention, consume interleavings and counters");
}
int main(int argc,char **argv)
{
    assert(argc==2);
    SRNOS_NetworkConfig c={.enabled=true,.address={192,168,1,50},.netmask={255,255,255,0},.udp_ports={5000,5001},.udp_port_count=2};
    assert(SRNOS_NetworkConfigValid(&c));
    SRNOS_NetworkConfig bad=c;bad.netmask[2]=1;assert(!SRNOS_NetworkConfigValid(&bad));
    bad=c;bad.address[3]=0;assert(!SRNOS_NetworkConfigValid(&bad));
    bad=c;bad.address[0]=224;assert(!SRNOS_NetworkConfigValid(&bad));
    bad=c;bad.gateway[0]=10;assert(!SRNOS_NetworkConfigValid(&bad));
    bad=c;bad.udp_ports[1]=5000;assert(!SRNOS_NetworkConfigValid(&bad));
    if(!strcmp(argv[1],"custom")) { c.address[2]=42;host_ip[2]=42;c.udp_port_count=4;c.udp_ports[2]=5002;c.udp_ports[3]=5003; }
    if(!strcmp(argv[1],"ping_only")) c.udp_port_count=0;
    if(!strcmp(argv[1],"policy")) c.udp_rx_select[0]=select_packet;
    if(!strcmp(argv[1],"disabled")) {
        c.enabled=false;assert(SRNOS_NetworkPrepare(&c));assert(!claimed);
        assert(SRNOS_NetworkStart() && !start_entry);return 0;
    }
    assert(SRNOS_NetworkPrepare(&c) && claimed && SRNOS_NetworkStart());start_entry(NULL);
    assert(initialized && published.ready && published.link_up);sent=0;
    if(!strcmp(argv[1],"policy")) { test_policy();return 0; }
    arp(1);feed();assert(sent==1 && outgoing[0].data[21]==2);sent=0;
    /* Real lwIP ICMP echo, short and maximum unfragmented payload. */
    for(unsigned n=0;n<=1472;n+=1472) {
        ip_frame(1,n);feed();assert(sent==1);verify_ipv4(&outgoing[0],1);
        assert(outgoing[0].data[34]==0 && outgoing[0].length==42+n);sent=0;
    }
    if(!strcmp(argv[1],"ping_only")) { puts("PASS: Ping without any UDP listeners");return 0; }
    /* Actual UDP parser and raw API, checksums and payload lengths. */
    const unsigned lengths[]={0,1,17,512,1472};
    SRNOS_UDPDatagram d;
    for(unsigned j=0;j<sizeof(lengths)/sizeof(*lengths);++j) {
        unsigned n=lengths[j];ip_frame(17,n);feed();
        assert(SRNOS_UDPReceive(5000,&d)==SRNOS_NETWORK_OK && d.length==n && d.source_port==40000);
        for(unsigned i=0;i<n;++i)assert(d.data[i]==(uint8_t)(i*17));
        assert(SRNOS_UDPSend(5000,d.source_address,d.source_port,d.data,d.length)==SRNOS_NETWORK_OK);
        step(NULL);assert(sent==1);verify_ipv4(&outgoing[0],17);assert(outgoing[0].length==42+n);sent=0;
    }
    /* Isolated ports, newest-drop RX, stale receive and invalid API inputs. */
    ip_frame(17,1);feed();ip_frame(17,17);feed();
    assert(current.udp_rx_dropped==1 && SRNOS_UDPReceive(5000,&d)==0 && d.length==1);
    assert(SRNOS_UDPReceive(5001,&d)==SRNOS_NETWORK_EMPTY);
    ip_frame(17,17);feed();clock_ms+=101;assert(SRNOS_UDPReceive(5000,&d)==SRNOS_NETWORK_EMPTY);
    assert(SRNOS_UDPSend(5000,host_ip,1,NULL,1)==SRNOS_NETWORK_INVALID);
    assert(SRNOS_UDPSend(5000,host_ip,1,&d,1473)==SRNOS_NETWORK_INVALID);
    assert(SRNOS_UDPSend(6000,host_ip,1,NULL,0)==SRNOS_NETWORK_INVALID);
    const uint8_t broadcast[]={192,168,1,255};assert(!SRNOS_NetworkDestinationValid(&c,broadcast));
    task_context=false;assert(SRNOS_UDPSend(5000,host_ip,1,NULL,0)==SRNOS_NETWORK_CONTEXT);task_context=true;
    /* Corrupt UDP/IP/ICMP checksums, fragments, TCP, IPv6, foreign destination. */
    for(unsigned kind=0;kind<7;++kind) {
        ip_frame(kind==2?1:17,17);
        if(kind==0)incoming.data[45]^=1;
        if(kind==1)incoming.data[24]^=1;
        if(kind==2)incoming.data[36]^=1;
        if(kind==3) { incoming.data[20]=0x20;incoming.data[24]=incoming.data[25]=0;put16(incoming.data+24,checksum(incoming.data+14,20,0)); }
        if(kind==4)incoming.data[23]=6;
        if(kind==5)put16(incoming.data+12,0x86dd);
        if(kind==6)incoming.data[33]^=1;
        feed();assert(SRNOS_UDPReceive(5000,&d)==SRNOS_NETWORK_EMPTY && sent==0);
    }
    /* TX capacity and ARP pending expiry: no stale application packet in lwIP. */
    uint8_t unknown[4];memcpy(unknown,host_ip,4);unknown[3]=101;
    assert(SRNOS_UDPSend(5000,unknown,9,NULL,0)==0);
    assert(SRNOS_UDPSend(5000,host_ip,9,NULL,0)==0);
    assert(SRNOS_UDPSend(5000,host_ip,9,NULL,0)==SRNOS_NETWORK_FULL);
    step(NULL);assert(pending && sent==1 && outgoing[0].data[13]==6);sent=0;
    clock_ms+=101;step(NULL);assert(!pending && current.udp_tx_dropped==1);
    step(NULL);assert(current.udp_tx_dropped==2 && sent==0);
    /* Missed down/up still invalidates epoch, including an unread RX datagram. */
    ip_frame(17,1);feed();assert(SRNOS_UDPSend(5000,host_ip,9,NULL,0)==0);
    ++ethernet.link_epoch;
    assert(SRNOS_UDPReceive(5000,&d)==SRNOS_NETWORK_NOT_READY);
    step(NULL);sent=0;assert(SRNOS_UDPReceive(5000,&d)==SRNOS_NETWORK_EMPTY);
    ethernet.state=SRNOS_ETHERNET_LINK_DOWN;step(NULL);
    assert(SRNOS_UDPSend(5000,host_ip,9,NULL,0)==SRNOS_NETWORK_NOT_READY);
    ethernet.state=SRNOS_ETHERNET_LINK_UP;++ethernet.link_epoch;step(NULL);sent=0;
    /* Malformed length/header stress through the real parser, ASan/UBSan. */
    for(unsigned n=14;n<160;++n) {
        ip_frame(17,17);incoming.length=n;
        for(unsigned k=14;k<n;++k)incoming.data[k]=(uint8_t)(n*17+k*31);
        feed();sent=0;
    }
    puts("PASS: actual lwIP ARP/Ping/UDP, configuration, checksums, limits, epochs and bounded queues");
    return 0;
}
