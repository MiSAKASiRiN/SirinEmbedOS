#pragma once
#include "SirinEmbedOS/IO/Serial/Serial.h"
#include "SirinEmbedOS/Services/RemoteControl/lib_remote_control.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
 SRNOS_SerialPortId dr16_port,vt03_port;
 rc_source_t primary,secondary;
 uint16_t idle_ms,offline_ms,stick_deadzone;
 rc_post_map_hook_t post_map_hook;
} SRNOS_RemoteControlConfig;
/* Configure once after SerialConfigure, before SerialStart; no task is needed
 * when both ports are NONE. Start creates the single protocol/arbitration task. */
int SRNOS_RemoteControlConfigure(const SRNOS_RemoteControlConfig *);
int SRNOS_RemoteControlStart(void);
#ifdef __cplusplus
}
#endif
