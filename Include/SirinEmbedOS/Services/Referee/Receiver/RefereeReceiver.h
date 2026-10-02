#ifndef SRNOS_REFEREE_RECEIVER_H
#define SRNOS_REFEREE_RECEIVER_H

#include "SirinEmbedOS/Services/Referee/referee.h"

#ifdef __cplusplus
extern "C" {
#endif

void SRNOS_RefereeTask(void *argument);

typedef struct {
  SRNOS_SerialChannel *channel;
} SRNOS_RefereeConfig;

typedef struct {
  uint32_t update_tick_ms;
  uint32_t last_rx_ms;
  uint8_t  online;
  uint8_t  reserved[3];
  SRNOS_RefereeGameStatus game_status;          // 0x0001
  SRNOS_RefereeRobotState robot_state;          // 0x0201
  SRNOS_RefereePowerHeatData power_heat_data;  // 0x0202
  SRNOS_RefereeRobotHurt robot_hurt;            // 0x0206
  SRNOS_RefereeShootData shoot_data;            // 0x0207
} SRNOS_RefereeDebugWatch;

// Debug-visible snapshot.
extern volatile SRNOS_RefereeRobotState SRNOS_DebugRefereeRobotStateDbg;
extern volatile uint32_t SRNOS_DebugRefereeLastRxMsDbg;
extern volatile uint8_t SRNOS_DebugRefereeOnlineDbg;
// Kept for compatibility; actual refresh interval is configured by SRNOS_REFEREE_DEBUG_REFRESH_MS.
extern volatile SRNOS_RefereeDebugWatch SRNOS_DebugRefereeDebugWatch100ms;

// Thread-safe read helper for other tasks.
bool SRNOS_RefereeGetDebugWatch(SRNOS_RefereeDebugWatch *out);

#ifdef __cplusplus
}
#endif

#endif // SRNOS_REFEREE_RECEIVER_H
