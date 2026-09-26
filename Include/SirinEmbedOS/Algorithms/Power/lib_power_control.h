// power_control.h
#ifndef H723VG_V2_FREERTOS_LIB_POWER_CONTROL_H
#define H723VG_V2_FREERTOS_LIB_POWER_CONTROL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


#define POWER_CONTROL_MAX_MOTORS 16u

#define POWER_CONTROL_CMD_CURRENT 0u
#define POWER_CONTROL_CMD_VOLTAGE 1u

#define POWER_CONTROL_PRIORITY_NONCRIT 0u
#define POWER_CONTROL_PRIORITY_CRIT    1u
#define POWER_CONTROL_PRIORITY_STRONG  2u

#define POWER_CONTROL_DEFAULT_Q_J                 60.0f
#define POWER_CONTROL_DEFAULT_PL_FALLBACK_W       40.0f
#define POWER_CONTROL_DEFAULT_Z_MIN_J             10.0f
#define POWER_CONTROL_DEFAULT_Z_TARGET_J          30.0f
#define POWER_CONTROL_DEFAULT_T_WINDOW_S          0.10f
#define POWER_CONTROL_DEFAULT_P_BURST_MAX_W       200.0f
#define POWER_CONTROL_DEFAULT_BUDGET_FILTER_ALPHA 0.20f
#define POWER_CONTROL_DEFAULT_Z_UPDATE_PERIOD_S   0.10f
#define POWER_CONTROL_DEFAULT_Z_OFFLINE_CAP_J     10.0f
#define POWER_CONTROL_DEFAULT_Z_OFFLINE_DECAY_JS  10.0f
#define POWER_CONTROL_DEFAULT_CMD_LIMIT           16384

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  float speed_rpm;
  float current_feedback;
  float current_cmd;
} motor_meas_t;

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
} power_control_motor_cfg_t;

typedef struct {
  uint32_t motor_count;
  power_control_motor_cfg_t motors[POWER_CONTROL_MAX_MOTORS];

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
} power_control_config_t;

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
} power_control_debug_t;

typedef struct {
  const power_control_config_t *cfg;

  float z_hat;
  float pl_hat;
  bool  online;
  float z_meas_last;
  float z_meas_timer_s;
  float budget_filt;

  int16_t last_cmd[POWER_CONTROL_MAX_MOTORS];
  power_control_debug_t dbg;
} power_control_ctx_t;

void PowerControl_Init(power_control_ctx_t *ctx, const power_control_config_t *cfg);
void PowerControl_Reset(power_control_ctx_t *ctx);
void PowerControl_Update1kHz(power_control_ctx_t *ctx,
                             float dt,
                             bool budget_source_online,
                             float Pl_watt,
                             float Z_joule,
                             float P_meas_watt,
                             bool P_meas_valid,
                             const motor_meas_t *meas,
                             int16_t *cmd_inout,
                             uint32_t motor_count);

bool PowerControl_GetDebug(const power_control_ctx_t *ctx, power_control_debug_t *out);

/*
Example (call before CAN send, after chassis control output):
  power_control_config_t cfg = {0};
  cfg.motor_count = 8;
  cfg.pl_fallback_watt = 40.0f;
  cfg.q_joule = 60.0f;
  for (int i = 0; i < 4; ++i) {
    cfg.motors[i].enabled = true;
    cfg.motors[i].priority = POWER_CONTROL_PRIORITY_NONCRIT;
    cfg.motors[i].cmd_type = POWER_CONTROL_CMD_CURRENT;
    cfg.motors[i].cmd_limit = 16000;
    cfg.motors[i].k_iw = 0.002f;
    cfg.motors[i].use_feedback_current = true;
  }
  for (int i = 0; i < 4; ++i) {
    cfg.motors[4 + i].enabled = true;
    cfg.motors[4 + i].priority = POWER_CONTROL_PRIORITY_CRIT;
    cfg.motors[4 + i].cmd_type = POWER_CONTROL_CMD_VOLTAGE;
    cfg.motors[4 + i].cmd_limit = 16000;
    cfg.motors[4 + i].slew_rate = 8000.0f;
    cfg.motors[4 + i].k_iw = 0.001f;
    cfg.motors[4 + i].use_feedback_current = true;
  }
  power_control_ctx_t pc;
  PowerControl_Init(&pc, &cfg);

  // 1 kHz loop:
  PowerControl_Update1kHz(&pc, 0.001f, budget_online, Pl, Z, 0.0f, false, meas, cmd, 8);
*/

#ifdef __cplusplus
}
#endif

#endif  // H723VG_V2_FREERTOS_POWER_CONTROL_H
