#include "SirinEmbedOS/Services/Referee/Transmitter/tskptt_referee_tx.h"
#include <string.h>
#include "SirinEmbedOS/Services/Referee/referee.h"
static SRNOS_SerialChannel *channel;
volatile referee_tx_debug_t g_referee_tx_debug;
bool RefereeTX_BindChannel(SRNOS_SerialChannel *c) {
 if(!c||c!=Referee_GetBoundChannel()||(channel&&channel!=c))return false;channel=c;return true;
}
size_t RefereeTX_Write(const void *data,size_t len,TickType_t timeout) {
 g_referee_tx_debug.write_req_cnt++;
 if(SRNOS_SerialWrite(channel,data,len,(uint32_t)((uint64_t)timeout*1000/configTICK_RATE_HZ))!=SRNOS_SERIAL_OK){g_referee_tx_debug.write_fail_cnt++;return 0;}
 g_referee_tx_debug.write_ok_cnt++;return len;
}
size_t RefereeTX_WriteStr(const char *s,TickType_t timeout){return s?RefereeTX_Write(s,strlen(s),timeout):0;}
bool RefereeTX_Send(const uint8_t *d,uint16_t n,TickType_t timeout){return n&&RefereeTX_Write(d,n,timeout)==n;}
/* Serial's common worker now owns the TX queue; no redundant UART worker. */
void TSKPTT_Referee_TX_Task(void *arg){(void)arg;vTaskDelete(NULL);}
