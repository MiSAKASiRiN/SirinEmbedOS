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
 /* Legacy VT02/VT12 video-link UART, 115200/8N1. Independent of referee_port.
  * Publishes keyboard/mouse-only "vt02" Input; no analog channels or gear.
  * 0302 is copied to Message (VT02 transport, 0x22/1, endpoint=vt02_port).
  * Optional Serial tx_enabled permits explicit SRNOS_VT02Send/Flush; no auto TX. */
 SRNOS_SerialPortId vt02_port;
} SRNOS_RemoteControlConfig;
/* Independent named sources, no arbitration. One protocol worker shared with
 * the generic message UART. All-zero W.Bus calibration leaves values raw. */
int SRNOS_RemoteControlConfigure(const SRNOS_RemoteControlConfig *);
int SRNOS_RemoteControlStart(void);
#ifdef __cplusplus
}
#endif
