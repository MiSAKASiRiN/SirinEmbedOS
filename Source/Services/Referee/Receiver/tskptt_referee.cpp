#include "SirinEmbedOS/Services/Referee/Receiver/tskptt_referee.h"

#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

#ifndef REFEREE_TASK_PERIOD_MS
#define REFEREE_TASK_PERIOD_MS 1u
#endif

#ifndef REFEREE_ONLINE_TIMEOUT_MS
#define REFEREE_ONLINE_TIMEOUT_MS 100u
#endif

#ifndef REFEREE_DEBUG_REFRESH_MS
#define REFEREE_DEBUG_REFRESH_MS 20u
#endif

extern "C" {
  volatile referee_robot_state_t g_referee_robot_state_dbg;
  volatile uint32_t g_referee_last_rx_ms_dbg = 0;
  volatile uint8_t g_referee_online_dbg = 0;
  volatile bool gimbal_on;
  volatile bool chassis_on;
  volatile bool shooter_on;
  volatile referee_debug_watch_t g_referee_debug_watch_100ms;
}

static volatile uint32_t g_referee_debug_watch_seq = 0;

extern "C" void TSKPTT_Referee_Task(void *argument)
{
  const tskptt_referee_config_t *config =
      static_cast<const tskptt_referee_config_t *>(argument);

  (void)config; /* Binding and initialization are completed before startup. */
  if (!Referee_Start()) {osThreadExit();return;}
  referee_snapshot_t snap = {};
  uint32_t last_dbg_tick = 0;
  for (;;) {
    Referee_Poll();

    const uint32_t now_tick = osKernelGetTickCount();
    if ((uint32_t)(now_tick - last_dbg_tick) >= REFEREE_DEBUG_REFRESH_MS) {
      last_dbg_tick = now_tick;

      Referee_GetSnapshot(&snap);
      memcpy((void *)&g_referee_robot_state_dbg, &snap.robot_state, sizeof(g_referee_robot_state_dbg));
      g_referee_last_rx_ms_dbg = snap.last_rx_ms;
      g_referee_online_dbg = Referee_IsOnline(REFEREE_ONLINE_TIMEOUT_MS) ? 1u : 0u;
      gimbal_on  = (snap.robot_state.power_management_gimbal_output & (1u << 0)) != 0;
      chassis_on = (snap.robot_state.power_management_chassis_output & (1u << 1)) != 0;
      shooter_on = (snap.robot_state.power_management_shooter_output & (1u << 2)) != 0;

      taskENTER_CRITICAL();
      __atomic_fetch_add(&g_referee_debug_watch_seq, 1u, __ATOMIC_ACQ_REL);
      g_referee_debug_watch_100ms.update_tick_ms = now_tick;
      g_referee_debug_watch_100ms.last_rx_ms = snap.last_rx_ms;
      g_referee_debug_watch_100ms.online = g_referee_online_dbg;
      g_referee_debug_watch_100ms.reserved[0] = 0u;
      g_referee_debug_watch_100ms.reserved[1] = 0u;
      g_referee_debug_watch_100ms.reserved[2] = 0u;
      memcpy((void *)&g_referee_debug_watch_100ms.game_status,
             &snap.game_status,
             sizeof(g_referee_debug_watch_100ms.game_status));
      memcpy((void *)&g_referee_debug_watch_100ms.robot_state,
             &snap.robot_state,
             sizeof(g_referee_debug_watch_100ms.robot_state));
      memcpy((void *)&g_referee_debug_watch_100ms.power_heat_data,
             &snap.power_heat_data,
             sizeof(g_referee_debug_watch_100ms.power_heat_data));
      memcpy((void *)&g_referee_debug_watch_100ms.robot_hurt,
             &snap.robot_hurt,
             sizeof(g_referee_debug_watch_100ms.robot_hurt));
      memcpy((void *)&g_referee_debug_watch_100ms.shoot_data,
             &snap.shoot_data,
             sizeof(g_referee_debug_watch_100ms.shoot_data));
      __atomic_fetch_add(&g_referee_debug_watch_seq, 1u, __ATOMIC_RELEASE);
      taskEXIT_CRITICAL();
    }

    osDelay(REFEREE_TASK_PERIOD_MS);
  }
}

extern "C" bool TSKPTT_Referee_GetDebugWatch(referee_debug_watch_t *out)
{
  if (out == NULL) {
    return false;
  }

  while (true) {
    const uint32_t seq1 = __atomic_load_n(&g_referee_debug_watch_seq, __ATOMIC_ACQUIRE);
    if (seq1 & 1u) {
      continue;
    }

    memcpy(out, (const void *)&g_referee_debug_watch_100ms, sizeof(*out));

    const uint32_t seq2 = __atomic_load_n(&g_referee_debug_watch_seq, __ATOMIC_ACQUIRE);
    if (seq1 == seq2) {
      return true;
    }
  }
}
