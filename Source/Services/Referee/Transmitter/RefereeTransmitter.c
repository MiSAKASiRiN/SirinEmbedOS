#include "SirinEmbedOS/Services/Referee/Transmitter/RefereeTransmitter.h"
#include <string.h>
#include "SirinEmbedOS/Services/Referee/referee.h"
static SRNOS_SerialChannel *channel;
volatile SRNOS_RefereeTxDebug SRNOS_DebugRefereeTxDebug;
bool SRNOS_RefereeTxBindChannel(SRNOS_SerialChannel *c) {
 if(!c||c!=SRNOS_RefereeGetBoundChannel()||(channel&&channel!=c))return false;channel=c;return true;
}
size_t SRNOS_RefereeTxWrite(const void *data,size_t len,uint32_t timeout) {
 SRNOS_DebugRefereeTxDebug.write_req_cnt++;
 if(SRNOS_SerialWrite(channel,data,len,timeout)!=SRNOS_SERIAL_OK){SRNOS_DebugRefereeTxDebug.write_fail_cnt++;return 0;}
 SRNOS_DebugRefereeTxDebug.write_ok_cnt++;return len;
}
size_t SRNOS_RefereeTxWriteStr(const char *s,uint32_t timeout){return s?SRNOS_RefereeTxWrite(s,strlen(s),timeout):0;}
bool SRNOS_RefereeTransmit(const uint8_t *d,uint16_t n,uint32_t timeout){return n&&SRNOS_RefereeTxWrite(d,n,timeout)==n;}
/* Serial's common worker now owns the TX queue; no redundant UART worker. */
