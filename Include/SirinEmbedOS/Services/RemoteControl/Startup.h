#pragma once
#include "SirinEmbedOS/IO/Serial/Serial.h"
#include "SirinEmbedOS/Services/Input.h"
#include "SirinEmbedOS/Services/Message.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
 SRNOS_SerialPortId dr16_port,vt03_port,wbus_port;
 uint16_t offline_ms;
 SRNOS_InputChannelDescriptor wbus_channels[SRNOS_INPUT_CHANNELS];
 uint8_t sirinrmt_transports; /* HOST/UART/UDP/VIDEO bits; transport enabled separately. */
 /* Multiplex 0311/0310 with VT03 on vt03_port. Requires RX and TX enabled.
  * Uses the existing remote worker, no second Serial reader or new task. */
 bool video_link_enabled;
} SRNOS_RemoteControlConfig;
/* Independent named sources, no arbitration. One protocol worker shared with
 * the generic message UART. All-zero W.Bus calibration leaves values raw. */
int SRNOS_RemoteControlConfigure(const SRNOS_RemoteControlConfig *);
int SRNOS_RemoteControlStart(void);
#ifdef __cplusplus
}
#endif
