#include <cassert>
#include <cstdio>
#include "../../../Source/IO/RS485/rs485_router.cpp"
static const void *bus_owner;
static unsigned queued;
static SRNOS_SerialConfig config{SRNOS_SERIAL_UART7,115200,8,1,SRNOS_SERIAL_PARITY_NONE,SRNOS_SERIAL_RS485,true,true,false};
extern "C" SRNOS_SerialChannel *SRNOS_SerialGetChannel(SRNOS_SerialPortId p){return p==SRNOS_SERIAL_UART7?reinterpret_cast<SRNOS_SerialChannel*>(1):nullptr;}
extern "C" SRNOS_SerialStatus SRNOS_SerialClaim(SRNOS_SerialChannel *,const void *owner){if(bus_owner&&owner!=bus_owner)return SRNOS_SERIAL_CONFLICT;bus_owner=owner;return SRNOS_SERIAL_OK;}
extern "C" const SRNOS_SerialConfig *SRNOS_SerialGetConfig(const SRNOS_SerialChannel *){return &config;}
extern "C" SRNOS_SerialStatus SRNOS_SerialRead(SRNOS_SerialChannel *,void *,size_t,size_t *,uint32_t){return SRNOS_SERIAL_TIMEOUT;}
extern "C" SRNOS_SerialStatus SRNOS_SerialTransceive(SRNOS_SerialChannel *,const SRNOS_SerialTransaction *,size_t *){return SRNOS_SERIAL_TIMEOUT;}
osKernelState_t osKernelGetState(){return osKernelReady;}
uint32_t osKernelGetTickFreq(){return 1000;}uint32_t osKernelGetTickCount(){return 100;}
osMessageQueueId_t osMessageQueueNew(uint32_t,uint32_t,const void*){return reinterpret_cast<void*>(1);}
osStatus_t osMessageQueuePut(osMessageQueueId_t,const void*,uint8_t,uint32_t){queued++;return osOK;}
osStatus_t osMessageQueueGet(osMessageQueueId_t,void*,uint8_t*,uint32_t){return osError;}
osThreadId_t osThreadNew(void(*)(void*),void*,const osThreadAttr_t*){return reinterpret_cast<void*>(1);}
static void append_crc(uint8_t *p,size_t n){uint16_t v=crc(p,n);p[n]=v;p[n+1]=v>>8;}
int main(){
 int first_owner,second_owner;SRNOS_RS485RouterHandle a=0,b=0;
 assert(SRNOS_RS485RouterRegisterModbus(1,1,&first_owner,nullptr,nullptr,&a)==0);
 assert(SRNOS_RS485RouterRegisterModbus(1,1,&second_owner,nullptr,nullptr,&b)==SRNOS_RS485_ROUTER_ERR_DUP);
 assert(SRNOS_RS485RouterRegisterModbus(1,2,&second_owner,nullptr,nullptr,&b)==0);
 assert(SRNOS_RS485Start(100)==0&&SRNOS_RS485IsFrozen());
 assert(SRNOS_RS485RouterUnregister(a)==SRNOS_RS485_ROUTER_ERR_STATE);
 assert(SRNOS_RS485RouterRegisterModbus(1,3,&second_owner,nullptr,nullptr,&b)==SRNOS_RS485_ROUTER_ERR_STATE);
 uint8_t request[]={1,3,0,0,0,2,0xc4,0x0b};assert(crc(request,8)==0);
 assert(SRNOS_RS485RouterSendModbusAdu(a,request,8,0)==0&&queued==1);
 request[0]=2;append_crc(request,6);assert(SRNOS_RS485RouterSendModbusAdu(a,request,8,0)==SRNOS_RS485_ROUTER_ERR_PARAM);
 request[0]=0;append_crc(request,6);assert(SRNOS_RS485RouterSendModbusAdu(a,request,8,0)==0&&queued==2);
 request[7]^=1;assert(SRNOS_RS485RouterSendModbusAdu(a,request,8,0)==SRNOS_RS485_ROUTER_ERR_CRC);
 uint8_t response[]={1,3,4,0,1,0,2,0,0};append_crc(response,7);match_context ctx{1,3};
 assert(SRNOS_ModbusMatch(response,5,&ctx)==0);assert(SRNOS_ModbusMatch(response,9,&ctx)==9);
 response[0]=2;assert(SRNOS_ModbusMatch(response,9,&ctx)==-1);response[0]=1;
 response[8]^=1;assert(SRNOS_ModbusMatch(response,9,&ctx)==-1);
 uint8_t exception[]={1,0x83,2,0,0};append_crc(exception,3);assert(SRNOS_ModbusMatch(exception,5,&ctx)==5);
 puts("RS485 endpoint freeze, unit ownership, CRC, broadcast and response matching: PASS");
}
