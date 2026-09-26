#ifndef TSKPTT_REFEREE_H
#define TSKPTT_REFEREE_H

#include "SirinEmbedOS/Services/Referee/referee.h"

#ifdef __cplusplus
extern "C" {
#endif

void TSKPTT_Referee_Task(void *argument);

typedef struct {
  SRNOS_SerialChannel *channel;
} tskptt_referee_config_t;

typedef struct {
  uint32_t update_tick_ms;
  uint32_t last_rx_ms;
  uint8_t  online;
  uint8_t  reserved[3];
  referee_game_status_t game_status;          // 0x0001
  referee_robot_state_t robot_state;          // 0x0201
  referee_power_heat_data_t power_heat_data;  // 0x0202
  referee_robot_hurt_t robot_hurt;            // 0x0206
  referee_shoot_data_t shoot_data;            // 0x0207
} referee_debug_watch_t;

// Debug-visible snapshot.
extern volatile referee_robot_state_t g_referee_robot_state_dbg;
extern volatile uint32_t g_referee_last_rx_ms_dbg;
extern volatile uint8_t g_referee_online_dbg;
// Kept for compatibility; actual refresh interval is configured by REFEREE_DEBUG_REFRESH_MS.
extern volatile referee_debug_watch_t g_referee_debug_watch_100ms;

// Thread-safe read helper for other tasks.
bool TSKPTT_Referee_GetDebugWatch(referee_debug_watch_t *out);

#ifdef __cplusplus
}
#endif

#endif // TSKPTT_REFEREE_H
