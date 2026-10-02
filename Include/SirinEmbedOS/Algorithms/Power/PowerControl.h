// power_control.h
#ifndef SRNOS_POWER_CONTROL_H
#define SRNOS_POWER_CONTROL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


#define SRNOS_POWER_CONTROL_MAX_MOTORS 16u

#define SRNOS_POWER_CONTROL_CMD_CURRENT 0u
#define SRNOS_POWER_CONTROL_CMD_VOLTAGE 1u

#define SRNOS_POWER_CONTROL_PRIORITY_NONCRIT 0u
#define SRNOS_POWER_CONTROL_PRIORITY_CRIT    1u
#define SRNOS_POWER_CONTROL_PRIORITY_STRONG  2u

#define SRNOS_POWER_CONTROL_DEFAULT_Q_J                 60.0f
#define SRNOS_POWER_CONTROL_DEFAULT_PL_FALLBACK_W       40.0f
#define SRNOS_POWER_CONTROL_DEFAULT_Z_MIN_J             10.0f
#define SRNOS_POWER_CONTROL_DEFAULT_Z_TARGET_J          30.0f
#define SRNOS_POWER_CONTROL_DEFAULT_T_WINDOW_S          0.10f
#define SRNOS_POWER_CONTROL_DEFAULT_P_BURST_MAX_W       200.0f
#define SRNOS_POWER_CONTROL_DEFAULT_BUDGET_FILTER_ALPHA 0.20f
#define SRNOS_POWER_CONTROL_DEFAULT_Z_UPDATE_PERIOD_S   0.10f
#define SRNOS_POWER_CONTROL_DEFAULT_Z_OFFLINE_CAP_J     10.0f
#define SRNOS_POWER_CONTROL_DEFAULT_Z_OFFLINE_DECAY_JS  10.0f
#define SRNOS_POWER_CONTROL_DEFAULT_CMD_LIMIT           16384

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  float speed_rpm;
  float current_feedback;
  float current_cmd;
} SRNOS_PowerMotorMeasurement;

typedef struct {
  bool    enabled;
  uint8_t priority;
  uint8_t cmd_type;
  int16_t cmd_limit;
  float   slew_rate;
  float   k_iw;
  float   k_w2;
  float   k_i2;
  float   k_0;
  bool    use_feedback_current;
} SRNOS_PowerControlMotorCfg;

typedef struct {
  uint32_t motor_count;
  SRNOS_PowerControlMotorCfg motors[SRNOS_POWER_CONTROL_MAX_MOTORS];

  float q_joule;
  float pl_fallback_watt;
  float z_min_joule;
  float z_target_joule;
  float t_window_s;
  float p_burst_max_watt;
  float budget_filter_alpha;
  float z_update_period_s;
  float z_offline_cap_joule;
  float z_offline_decay_rate;
} SRNOS_PowerControlConfig;

typedef struct {
  float P_total_est;
  float P_total_meas;
  float P_total_used;
  float P_total_after;
  float P_budget;
  float P_crit;
  float P_non;
  float Pl_hat;
  float Z_hat;
  float scale_non;
  float scale_all;
  uint32_t overflow_cnt;
} SRNOS_PowerControlDebug;

typedef struct {
  const SRNOS_PowerControlConfig *cfg;

  float z_hat;
  float pl_hat;
  bool  online;
  float z_meas_last;
  float z_meas_timer_s;
  float budget_filt;

  int16_t last_cmd[SRNOS_POWER_CONTROL_MAX_MOTORS];
  SRNOS_PowerControlDebug dbg;
} SRNOS_PowerControlCtx;

void SRNOS_PowerControlInit(SRNOS_PowerControlCtx *ctx, const SRNOS_PowerControlConfig *cfg);
void SRNOS_PowerControlReset(SRNOS_PowerControlCtx *ctx);
void SRNOS_PowerControlUpdate1kHz(SRNOS_PowerControlCtx *ctx,
                             float dt,
                             bool budget_source_online,
                             float Pl_watt,
                             float Z_joule,
                             float P_meas_watt,
                             bool P_meas_valid,
                             const SRNOS_PowerMotorMeasurement *meas,
                             int16_t *cmd_inout,
                             uint32_t motor_count);

bool SRNOS_PowerControlGetDebug(const SRNOS_PowerControlCtx *ctx, SRNOS_PowerControlDebug *out);

/*
Example (call before CAN send, after chassis control output):
  SRNOS_PowerControlConfig cfg = {0};
  cfg.motor_count = 8;
  cfg.pl_fallback_watt = 40.0f;
  cfg.q_joule = 60.0f;
  for (int i = 0; i < 4; ++i) {
    cfg.motors[i].enabled = true;
    cfg.motors[i].priority = SRNOS_POWER_CONTROL_PRIORITY_NONCRIT;
    cfg.motors[i].cmd_type = SRNOS_POWER_CONTROL_CMD_CURRENT;
    cfg.motors[i].cmd_limit = 16000;
    cfg.motors[i].k_iw = 0.002f;
    cfg.motors[i].use_feedback_current = true;
  }
  for (int i = 0; i < 4; ++i) {
    cfg.motors[4 + i].enabled = true;
    cfg.motors[4 + i].priority = SRNOS_POWER_CONTROL_PRIORITY_CRIT;
    cfg.motors[4 + i].cmd_type = SRNOS_POWER_CONTROL_CMD_VOLTAGE;
    cfg.motors[4 + i].cmd_limit = 16000;
    cfg.motors[4 + i].slew_rate = 8000.0f;
    cfg.motors[4 + i].k_iw = 0.001f;
    cfg.motors[4 + i].use_feedback_current = true;
  }
  SRNOS_PowerControlCtx pc;
  SRNOS_PowerControlInit(&pc, &cfg);

  // 1 kHz loop:
  SRNOS_PowerControlUpdate1kHz(&pc, 0.001f, budget_online, Pl, Z, 0.0f, false, meas, cmd, 8);
*/

#ifdef __cplusplus
}
#endif

#endif  // SRNOS_POWER_CONTROL_H
