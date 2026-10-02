#include "thread_internal.h"
#include "SirinEmbedOS/Services/Referee/Receiver/RefereeReceiver.h"

#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

#ifndef SRNOS_REFEREE_TASK_PERIOD_MS
#define SRNOS_REFEREE_TASK_PERIOD_MS 1u
#endif

#ifndef SRNOS_REFEREE_ONLINE_TIMEOUT_MS
#define SRNOS_REFEREE_ONLINE_TIMEOUT_MS 100u
#endif

#ifndef SRNOS_REFEREE_DEBUG_REFRESH_MS
#define SRNOS_REFEREE_DEBUG_REFRESH_MS 20u
#endif

extern "C" {
  volatile SRNOS_RefereeRobotState SRNOS_DebugRefereeRobotStateDbg;
  volatile uint32_t SRNOS_DebugRefereeLastRxMsDbg = 0;
  volatile uint8_t SRNOS_DebugRefereeOnlineDbg = 0;
  volatile bool SRNOS_RefereeGimbalOn;
  volatile bool SRNOS_RefereeChassisOn;
  volatile bool SRNOS_RefereeShooterOn;
  volatile SRNOS_RefereeDebugWatch SRNOS_DebugRefereeDebugWatch100ms;
}

static volatile uint32_t SRNOS_DebugRefereeDebugWatchSeq = 0;

extern "C" void SRNOS_RefereeTask(void *argument)
{
  const SRNOS_RefereeConfig *config =
      static_cast<const SRNOS_RefereeConfig *>(argument);

  (void)config; /* Binding and initialization are completed before startup. */
  if (!SRNOS_RefereeReceiverReady()) return;
  SRNOS_RefereeSnapshot snap = {};
  uint32_t last_dbg_tick = 0;
  auto step=[&](){
    SRNOS_RefereePoll();

    const uint32_t now_tick = osKernelGetTickCount();
    if ((uint32_t)(now_tick - last_dbg_tick) >= SRNOS_REFEREE_DEBUG_REFRESH_MS) {
      last_dbg_tick = now_tick;

      SRNOS_RefereeGetSnapshot(&snap);
      memcpy((void *)&SRNOS_DebugRefereeRobotStateDbg, &snap.robot_state, sizeof(SRNOS_DebugRefereeRobotStateDbg));
      SRNOS_DebugRefereeLastRxMsDbg = snap.last_rx_ms;
      SRNOS_DebugRefereeOnlineDbg = SRNOS_RefereeIsOnline(SRNOS_REFEREE_ONLINE_TIMEOUT_MS) ? 1u : 0u;
      SRNOS_RefereeGimbalOn  = (snap.robot_state.power_management_gimbal_output & (1u << 0)) != 0;
      SRNOS_RefereeChassisOn = (snap.robot_state.power_management_chassis_output & (1u << 1)) != 0;
      SRNOS_RefereeShooterOn = (snap.robot_state.power_management_shooter_output & (1u << 2)) != 0;

      taskENTER_CRITICAL();
      __atomic_fetch_add(&SRNOS_DebugRefereeDebugWatchSeq, 1u, __ATOMIC_ACQ_REL);
      SRNOS_DebugRefereeDebugWatch100ms.update_tick_ms = now_tick;
      SRNOS_DebugRefereeDebugWatch100ms.last_rx_ms = snap.last_rx_ms;
      SRNOS_DebugRefereeDebugWatch100ms.online = SRNOS_DebugRefereeOnlineDbg;
      SRNOS_DebugRefereeDebugWatch100ms.reserved[0] = 0u;
      SRNOS_DebugRefereeDebugWatch100ms.reserved[1] = 0u;
      SRNOS_DebugRefereeDebugWatch100ms.reserved[2] = 0u;
      memcpy((void *)&SRNOS_DebugRefereeDebugWatch100ms.game_status,
             &snap.game_status,
             sizeof(SRNOS_DebugRefereeDebugWatch100ms.game_status));
      memcpy((void *)&SRNOS_DebugRefereeDebugWatch100ms.robot_state,
             &snap.robot_state,
             sizeof(SRNOS_DebugRefereeDebugWatch100ms.robot_state));
      memcpy((void *)&SRNOS_DebugRefereeDebugWatch100ms.power_heat_data,
             &snap.power_heat_data,
             sizeof(SRNOS_DebugRefereeDebugWatch100ms.power_heat_data));
      memcpy((void *)&SRNOS_DebugRefereeDebugWatch100ms.robot_hurt,
             &snap.robot_hurt,
             sizeof(SRNOS_DebugRefereeDebugWatch100ms.robot_hurt));
      memcpy((void *)&SRNOS_DebugRefereeDebugWatch100ms.shoot_data,
             &snap.shoot_data,
             sizeof(SRNOS_DebugRefereeDebugWatch100ms.shoot_data));
      __atomic_fetch_add(&SRNOS_DebugRefereeDebugWatchSeq, 1u, __ATOMIC_RELEASE);
      taskEXIT_CRITICAL();
    }



  };
  auto wait=[]()->bool { osDelay(SRNOS_REFEREE_TASK_PERIOD_MS);return true; };
  SRNOS_KernelRunCpp(step,wait,0,false);
}

extern "C" bool SRNOS_RefereeGetDebugWatch(SRNOS_RefereeDebugWatch *out)
{
  if (out == NULL) {
    return false;
  }

  while (true) {
    const uint32_t seq1 = __atomic_load_n(&SRNOS_DebugRefereeDebugWatchSeq, __ATOMIC_ACQUIRE);
    if (seq1 & 1u) {
      continue;
    }

    memcpy(out, (const void *)&SRNOS_DebugRefereeDebugWatch100ms, sizeof(*out));

    const uint32_t seq2 = __atomic_load_n(&SRNOS_DebugRefereeDebugWatchSeq, __ATOMIC_ACQUIRE);
    if (seq1 == seq2) {
      return true;
    }
  }
}
