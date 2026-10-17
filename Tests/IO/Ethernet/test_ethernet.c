#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "IO/Ethernet/ethernet.c"
#include "IO/ethernet_rx.h"

static uint32_t clock_ms;
static bool task_context = true, board_ok = true, phy_up = true, dma_complete;
static unsigned resets, sends, pending_rx;
static uint32_t board_error;
static osThreadFunc_t start_entry;
static uint8_t sent[SRNOS_ETHERNET_FRAME_MAX];
static uint16_t sent_length;
uint32_t SRNOS_TimeMs(void) { return clock_ms; }
bool SRNOS_KernelTaskContext(void) { return task_context; }
osThreadId_t SRNOS_KernelThreadNew(osThreadFunc_t fn, void *arg, const osThreadAttr_t *attr)
{ (void)arg; assert(attr->priority < 24); start_entry = fn; return (void *)1; }
void SRNOS_KernelRunLoop(const SRNOS_InternalLoop *loop) { loop->step(loop->context); }
void xTaskNotifyGive(TaskHandle_t task) { assert(task); }
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *wake) { assert(task); *wake = 0; }
uint32_t ulTaskNotifyTake(BaseType_t clear, uint32_t timeout) { (void)clear; clock_ms += timeout; return 0; }
void vTaskDelay(uint32_t ticks) { clock_ms += ticks; }
QueueHandle_t xQueueCreateStatic(unsigned n, unsigned size, uint8_t *storage, StaticQueue_t *q)
{ *q = (StaticQueue_t){.capacity=n,.size=size,.storage=storage}; return q; }
int xQueueSend(QueueHandle_t q, const void *item, unsigned wait)
{
    assert(!wait);
    if (q->count == q->capacity) return 0;
    memcpy(q->storage + ((q->head + q->count) % q->capacity) * q->size, item, q->size);
    ++q->count; return 1;
}
int xQueueReceive(QueueHandle_t q, void *item, unsigned wait)
{
    assert(!wait);
    if (!q->count) return 0;
    memcpy(item, q->storage + q->head * q->size, q->size);
    q->head = (q->head + 1) % q->capacity; --q->count; return 1;
}
void SRNOS_EthernetBoardMAC(uint8_t mac[6]) { const uint8_t value[]={2,1,2,3,4,5}; memcpy(mac,value,6); }
bool SRNOS_EthernetBoardInit(uint8_t mac[6], uint32_t *hz) { (void)mac; *hz = 50000000; return board_ok; }
void SRNOS_EthernetBoardReset(void) { ++resets; board_error = 0; }
bool SRNOS_EthernetBoardReadPHY(uint8_t address, uint8_t reg, uint32_t *value)
{
    *value = 0xffff;
    if (address != 1) return true;
    switch (reg) {
    case SRNOS_LAN8720_ID1: *value=7; break;
    case SRNOS_LAN8720_ID2: *value=0xc0f1; break;
    case SRNOS_LAN8720_BSR: *value=phy_up?0x24:0; break;
    case SRNOS_LAN8720_SPECIAL: *value=0x1018; break;
    }
    return true;
}
bool SRNOS_EthernetBoardWritePHY(uint8_t address,uint8_t reg,uint32_t value)
{ assert(address==1 && reg==0 && value==0x1200); return true; }
bool SRNOS_EthernetBoardLink(uint16_t speed, bool full) { assert(speed==100 && full); return true; }
int SRNOS_EthernetBoardReceive(SRNOS_EthernetFrame *frame)
{ if(!pending_rx)return 0; --pending_rx; frame->length=60; memset(frame->data,0x5a,60); return 1; }
bool SRNOS_EthernetBoardSend(const uint8_t *data, uint16_t length)
{ assert(!tx_busy); memcpy(sent,data,length); sent_length=length; ++sends; return true; }
int SRNOS_EthernetBoardTxComplete(void) { return dma_complete?1:0; }
uint32_t SRNOS_EthernetBoardError(void) { return board_error; }

int main(int argc, char **argv)
{
    assert(argc==2);
    // Model the pinned HAL contract: delivery clears head, NOT tail.
    void *head = NULL, *tail = NULL;
    SRNOS_EthernetRxBuffer nodes[5] = {0};
    for (unsigned i = 0; i < 100; ++i) {
        SRNOS_EthernetRxBuffer *first = &nodes[i % 5];
        SRNOS_EthernetRxBuffer *second = &nodes[(i + 1) % 5];
        SRNOS_EthernetRxLink(&head, &tail, first, 60);
        assert(head == first && tail == first && !first->next);
        SRNOS_EthernetRxLink(&head, &tail, second, 20);
        assert(head == first && tail == second && first->next == second && !second->next);
        head = NULL; // HAL_ETH_ReadData return path.
    }
    SRNOS_EthernetConfig config={.enabled=true};
    assert(SRNOS_EthernetConfigValid(&config));
    config.mac[0]=1; assert(!SRNOS_EthernetConfigValid(&config)); config.mac[0]=0;
    assert(!SRNOS_LAN8720Identify(0xffff,0xffff));
    for(unsigned i=0;i<8;++i) {
        uint16_t speed=0; bool full=false;
        bool valid=SRNOS_LAN8720Link(0x24,0x1000|(i<<2),&speed,&full);
        assert(valid==(i==1 || i==2 || i==5 || i==6));
        if(valid) { assert(speed==((i&3)==1?10:100)); assert(full==(i>4)); }
        assert(!SRNOS_LAN8720Link(0,0x1000|(i<<2),&speed,&full));
    }
    if(!strcmp(argv[1],"disabled")) config.enabled=false;
    if(!strcmp(argv[1],"recovery")) board_ok=false;
    assert(SRNOS_EthernetPrepare(&config)); assert(!SRNOS_EthernetPrepare(&config));
    assert(SRNOS_EthernetStart());
    SRNOS_EthernetStatus status;
    if(!config.enabled) { assert(!start_entry); assert(SRNOS_EthernetGetStatus(&status)); assert(status.state==SRNOS_ETHERNET_DISABLED); return 0; }
    start_entry(NULL);
    if(!board_ok) {
        assert(current.state==SRNOS_ETHERNET_ERROR && resets==1);
        board_ok=true; clock_ms=999; step(NULL); assert(current.state==SRNOS_ETHERNET_ERROR);
        clock_ms=1000; step(NULL);
    }
    assert(SRNOS_EthernetGetStatus(&status)); assert(status.state==SRNOS_ETHERNET_LINK_UP);
    uint8_t data[SRNOS_ETHERNET_FRAME_MAX]={0}; memcpy(data+6,status.mac,6); data[12]=0x88;data[13]=0xb5;
    SRNOS_EthernetFrame received;
    assert(SRNOS_EthernetSend(data,13)==SRNOS_ETHERNET_INVALID);
    assert(SRNOS_EthernetSend(data,sizeof(data)+1)==SRNOS_ETHERNET_INVALID);
    data[6]^=2; assert(SRNOS_EthernetSend(data,60)==SRNOS_ETHERNET_INVALID); data[6]^=2;
    task_context=false; assert(SRNOS_EthernetSend(data,60)==SRNOS_ETHERNET_CONTEXT); task_context=true;
    if(!strcmp(argv[1],"queue")) {
        for(unsigned i=0;i<4;++i) assert(SRNOS_EthernetSend(data,sizeof(data))==SRNOS_ETHERNET_OK);
        assert(SRNOS_EthernetSend(data,60)==SRNOS_ETHERNET_FULL);
        data[20]=42; step(NULL); assert(sends==1 && sent[20]==0 && sent_length==sizeof(data));
        dma_complete=true; for(unsigned i=0;i<4;++i)step(NULL);
        assert(current.tx_completed==4);
        pending_rx=9;step(NULL); assert(saturated && current.rx_dropped==4);
        assert(SRNOS_EthernetReceive(&received)==SRNOS_ETHERNET_OK && received.data[0]==0x5a);
        clock_ms+=101; assert(SRNOS_EthernetReceive(&received)==SRNOS_ETHERNET_EMPTY);
    } else if(!strcmp(argv[1],"timeout")) {
        assert(SRNOS_EthernetSend(data,60)==SRNOS_ETHERNET_OK); step(NULL);
        clock_ms+=100;step(NULL); assert(resets==1 && !tx_busy && current.tx_errors==1);
        assert(SRNOS_EthernetSend(data,60)==SRNOS_ETHERNET_NOT_READY);
    } else if(!strcmp(argv[1],"disconnect")) {
        assert(SRNOS_EthernetSend(data,60)==SRNOS_ETHERNET_OK); step(NULL);
        phy_up=false; clock_ms+=250;step(NULL);assert(resets==1 && current.state==SRNOS_ETHERNET_LINK_DOWN && !tx_busy);
        phy_up=true;clock_ms+=1000;step(NULL);assert(current.state==SRNOS_ETHERNET_LINK_UP);
        assert(sends==1); /* No replay after reconnection. */
    } else if(!strcmp(argv[1],"expiry")) {
        assert(SRNOS_EthernetSend(data,60)==SRNOS_ETHERNET_OK);clock_ms+=101;step(NULL);
        assert(sends==0 && current.tx_dropped==1);
        assert(SRNOS_EthernetSend(data,60)==SRNOS_ETHERNET_OK); ++current.link_epoch;step(NULL);
        assert(sends==0 && current.tx_dropped==2);
    } else if(!strcmp(argv[1],"dma_error")) {
        board_error=0x80000001;step(NULL);assert(resets==1 && current.state==SRNOS_ETHERNET_ERROR);
    } else if(!strcmp(argv[1],"wrap")) {
        clock_ms=UINT32_MAX-50;next_link=clock_ms+250;
        assert(SRNOS_EthernetSend(data,60)==SRNOS_ETHERNET_OK); clock_ms+=75;step(NULL);assert(sends==1);
        clock_ms+=100;step(NULL);assert(resets==1 && current.tx_errors==1);
    }
    assert(SRNOS_EthernetNetworkSend(data,60,published.link_epoch+1)==SRNOS_ETHERNET_NOT_READY);
    network_owner=true;
    assert(SRNOS_EthernetSend(data,60)==SRNOS_ETHERNET_OWNED);
    assert(SRNOS_EthernetReceive(&received)==SRNOS_ETHERNET_OWNED);
    puts(argv[1]);return 0;
}
