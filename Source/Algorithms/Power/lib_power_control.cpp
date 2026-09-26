// power_control.cpp
#include "SirinEmbedOS/Algorithms/Power/lib_power_control.h"

/* ===================== SECTION: types/config ===================== */
static inline float pc_absf(float x) {
  return (x < 0.0f) ? -x : x;
}

static inline float pc_clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static inline float pc_cfg_or_default(float v, float dflt) {
  return (v > 0.0f) ? v : dflt;
}

static inline float pc_cfg_alpha(float v, float dflt) {
  float a = (v >= 0.0f) ? v : dflt;
  return pc_clampf(a, 0.0f, 1.0f);
}

static inline uint32_t pc_min_u32(uint32_t a, uint32_t b) {
  return (a < b) ? a : b;
}

/* ===================== SECTION: core math ===================== */
static float pc_pick_current(const power_control_motor_cfg_t *mcfg,
                             const motor_meas_t *meas,
                             int16_t cmd_fallback,
                             bool prefer_cmd) {
  if (!mcfg || !meas) return 0.0f;
  if (mcfg->use_feedback_current) return meas->current_feedback;
  if (prefer_cmd) return (float)cmd_fallback;
  if (meas->current_cmd != 0.0f) return meas->current_cmd;
  return (float)cmd_fallback;
}

static float pc_estimate_power(const power_control_motor_cfg_t *mcfg,
                               float omega_rpm,
                               float current) {
  if (!mcfg) return 0.0f;
  const float abs_i = pc_absf(current);
  const float abs_w = pc_absf(omega_rpm);
  float p = 0.0f;
  p += mcfg->k_iw * abs_i * abs_w;
  p += mcfg->k_w2 * (abs_w * abs_w);
  p += mcfg->k_i2 * (abs_i * abs_i);
  p += mcfg->k_0;
  return (p > 0.0f) ? p : 0.0f;
}

/* ===================== SECTION: scaling ===================== */
static int16_t pc_scale_cmd(int16_t cmd, float scale) {
  float v = (float)cmd * scale;
  if (v > 32767.0f) v = 32767.0f;
  if (v < -32768.0f) v = -32768.0f;
  return (int16_t)v;
}

static int16_t pc_apply_limit_slew(int16_t cmd_in,
                                   int16_t last_cmd,
                                   int16_t cmd_limit,
                                   float slew_rate,
                                   float dt) {
  float limit = (float)cmd_limit;
  if (limit < 0.0f) limit = -limit;
  if (limit <= 0.0f) limit = (float)POWER_CONTROL_DEFAULT_CMD_LIMIT;

  float cmd = (float)cmd_in;
  cmd = pc_clampf(cmd, -limit, limit);

  if (slew_rate > 0.0f) {
    const float max_delta = slew_rate * dt;
    const float lo = (float)last_cmd - max_delta;
    const float hi = (float)last_cmd + max_delta;
    cmd = pc_clampf(cmd, lo, hi);
  }

  return (int16_t)cmd;
}

/* ===================== SECTION: debug ===================== */
static void pc_debug_store(power_control_ctx_t *ctx,
                           float p_total_est,
                           float p_total_meas,
                           float p_total_used,
                           float p_after,
                           float p_budget,
                           float p_crit,
                           float p_non,
                           float scale_non,
                           float scale_all) {
  if (!ctx) return;
  ctx->dbg.P_total_est = p_total_est;
  ctx->dbg.P_total_meas = p_total_meas;
  ctx->dbg.P_total_used = p_total_used;
  ctx->dbg.P_total_after = p_after;
  ctx->dbg.P_budget = p_budget;
  ctx->dbg.P_crit = p_crit;
  ctx->dbg.P_non = p_non;
  ctx->dbg.Pl_hat = ctx->pl_hat;
  ctx->dbg.Z_hat = ctx->z_hat;
  ctx->dbg.scale_non = scale_non;
  ctx->dbg.scale_all = scale_all;
}

void PowerControl_Init(power_control_ctx_t *ctx, const power_control_config_t *cfg) {
  if (!ctx) return;
  ctx->cfg = cfg;
  PowerControl_Reset(ctx);
}

void PowerControl_Reset(power_control_ctx_t *ctx) {
  if (!ctx) return;
  ctx->z_hat = 0.0f;
  ctx->pl_hat = 0.0f;
  ctx->online = false;
  ctx->z_meas_last = 0.0f;
  ctx->z_meas_timer_s = 0.0f;
  ctx->budget_filt = 0.0f;
  for (uint32_t i = 0; i < POWER_CONTROL_MAX_MOTORS; ++i) {
    ctx->last_cmd[i] = 0;
  }
  ctx->dbg.P_total_est = 0.0f;
  ctx->dbg.P_total_meas = 0.0f;
  ctx->dbg.P_total_used = 0.0f;
  ctx->dbg.P_total_after = 0.0f;
  ctx->dbg.P_budget = 0.0f;
  ctx->dbg.P_crit = 0.0f;
  ctx->dbg.P_non = 0.0f;
  ctx->dbg.Pl_hat = 0.0f;
  ctx->dbg.Z_hat = 0.0f;
  ctx->dbg.scale_non = 1.0f;
  ctx->dbg.scale_all = 1.0f;
  ctx->dbg.overflow_cnt = 0u;
}

void PowerControl_Update1kHz(power_control_ctx_t *ctx,
                             float dt,
                             bool budget_source_online,
                             float Pl_watt,
                             float Z_joule,
                             float P_meas_watt,
                             bool P_meas_valid,
                             const motor_meas_t *meas,
                             int16_t *cmd_inout,
                             uint32_t motor_count) {
  if (!ctx || !ctx->cfg || !meas || !cmd_inout) return;
  if (dt <= 0.0f || dt > 0.1f) dt = 0.001f;

  const power_control_config_t *cfg = ctx->cfg;
  const float q_joule = pc_cfg_or_default(cfg->q_joule, POWER_CONTROL_DEFAULT_Q_J);
  const float pl_fallback = pc_cfg_or_default(cfg->pl_fallback_watt,
                                              POWER_CONTROL_DEFAULT_PL_FALLBACK_W);
  float z_min = pc_cfg_or_default(cfg->z_min_joule, POWER_CONTROL_DEFAULT_Z_MIN_J);
  const float t_window = pc_cfg_or_default(cfg->t_window_s, POWER_CONTROL_DEFAULT_T_WINDOW_S);
  const float p_burst_max = pc_cfg_or_default(cfg->p_burst_max_watt,
                                              POWER_CONTROL_DEFAULT_P_BURST_MAX_W);
  const float alpha = pc_cfg_alpha(cfg->budget_filter_alpha,
                                   POWER_CONTROL_DEFAULT_BUDGET_FILTER_ALPHA);
  const float z_update_period = pc_cfg_or_default(cfg->z_update_period_s,
                                                  POWER_CONTROL_DEFAULT_Z_UPDATE_PERIOD_S);
  float z_offline_cap = pc_cfg_or_default(cfg->z_offline_cap_joule,
                                          POWER_CONTROL_DEFAULT_Z_OFFLINE_CAP_J);
  const float z_offline_decay = pc_cfg_or_default(cfg->z_offline_decay_rate,
                                                  POWER_CONTROL_DEFAULT_Z_OFFLINE_DECAY_JS);

  if (z_min > q_joule) z_min = q_joule;
  if (z_offline_cap > q_joule) z_offline_cap = q_joule;

  uint32_t count = motor_count;
  if (cfg->motor_count > 0 && cfg->motor_count < count) count = cfg->motor_count;
  count = pc_min_u32(count, POWER_CONTROL_MAX_MOTORS);

  const bool was_online = ctx->online;
  const float pl_target = (Pl_watt > 0.0f) ? Pl_watt : pl_fallback;
  if (budget_source_online) {
    ctx->online = true;
    ctx->pl_hat = pl_target;
    const float z_meas = pc_clampf(Z_joule, 0.0f, q_joule);
    ctx->z_meas_timer_s += dt;
    if (!was_online ||
        z_meas != ctx->z_meas_last ||
        ctx->z_meas_timer_s >= z_update_period) {
      ctx->z_hat = z_meas;
      ctx->z_meas_last = z_meas;
      ctx->z_meas_timer_s = 0.0f;
    }
  } else {
    ctx->online = false;
    ctx->pl_hat = pl_target;
    ctx->z_meas_timer_s = 0.0f;
    if (ctx->z_hat > z_offline_cap) ctx->z_hat = z_offline_cap;
    if (z_offline_decay > 0.0f) {
      ctx->z_hat = pc_clampf(ctx->z_hat - z_offline_decay * dt, 0.0f, q_joule);
    }
  }

  float p_total_est = 0.0f;
  float p_crit = 0.0f;
  float p_non = 0.0f;
  for (uint32_t i = 0; i < count; ++i) {
    const power_control_motor_cfg_t *mcfg = &cfg->motors[i];
    if (!mcfg->enabled) continue;
    const float omega = meas[i].speed_rpm;
    const float current = pc_pick_current(mcfg, &meas[i], cmd_inout[i], false);
    const float p_i = pc_estimate_power(mcfg, omega, current);
    p_total_est += p_i;
    if (mcfg->priority >= POWER_CONTROL_PRIORITY_CRIT) p_crit += p_i;
    else p_non += p_i;
  }

  float p_total_meas = 0.0f;
  bool p_meas_valid = P_meas_valid;
  if (!p_meas_valid || P_meas_watt < 0.0f) {
    p_meas_valid = false;
  } else {
    p_total_meas = P_meas_watt;
  }
  const float p_total_used = p_meas_valid ? p_total_meas : p_total_est;

  ctx->z_hat = pc_clampf(ctx->z_hat - (p_total_used - ctx->pl_hat) * dt, 0.0f, q_joule);

  float p_budget = ctx->pl_hat;
  if (t_window > 0.0f) {
    const float headroom = (ctx->z_hat - z_min) / t_window;
    p_budget += pc_clampf(headroom, 0.0f, p_burst_max);
  }
  if (p_budget < 0.0f) p_budget = 0.0f;

  if (alpha <= 0.0f) ctx->budget_filt = p_budget;
  else ctx->budget_filt += alpha * (p_budget - ctx->budget_filt);
  p_budget = ctx->budget_filt;

  float scale_non = 1.0f;
  float scale_all = 1.0f;
  if (p_total_used > p_budget && p_total_used > 0.0f) {
    if (p_crit > p_budget) {
      scale_all = pc_clampf(p_budget / p_total_used, 0.0f, 1.0f);
      for (uint32_t i = 0; i < count; ++i) {
        const power_control_motor_cfg_t *mcfg = &cfg->motors[i];
        if (!mcfg->enabled) continue;
        cmd_inout[i] = pc_scale_cmd(cmd_inout[i], scale_all);
      }
    } else {
      const float non_budget = pc_clampf(p_budget - p_crit, 0.0f, p_budget);
      if (p_non > 0.0f) scale_non = pc_clampf(non_budget / p_non, 0.0f, 1.0f);
      else scale_non = 0.0f;
      for (uint32_t i = 0; i < count; ++i) {
        const power_control_motor_cfg_t *mcfg = &cfg->motors[i];
        if (!mcfg->enabled) continue;
        if (mcfg->priority == POWER_CONTROL_PRIORITY_NONCRIT) {
          cmd_inout[i] = pc_scale_cmd(cmd_inout[i], scale_non);
        }
      }
    }
  }

  for (uint32_t i = 0; i < count; ++i) {
    const power_control_motor_cfg_t *mcfg = &cfg->motors[i];
    if (!mcfg->enabled) {
      ctx->last_cmd[i] = cmd_inout[i];
      continue;
    }
    cmd_inout[i] = pc_apply_limit_slew(cmd_inout[i],
                                       ctx->last_cmd[i],
                                       mcfg->cmd_limit,
                                       mcfg->slew_rate,
                                       dt);
    ctx->last_cmd[i] = cmd_inout[i];
  }

  float p_after = 0.0f;
  for (uint32_t i = 0; i < count; ++i) {
    const power_control_motor_cfg_t *mcfg = &cfg->motors[i];
    if (!mcfg->enabled) continue;
    const float omega = meas[i].speed_rpm;
    const float current = pc_pick_current(mcfg, &meas[i], cmd_inout[i], true);
    p_after += pc_estimate_power(mcfg, omega, current);
  }

  pc_debug_store(ctx,
                 p_total_est,
                 p_total_meas,
                 p_total_used,
                 p_after,
                 p_budget,
                 p_crit,
                 p_non,
                 scale_non,
                 scale_all);
}

bool PowerControl_GetDebug(const power_control_ctx_t *ctx, power_control_debug_t *out) {
  if (!ctx || !out) return false;
  *out = ctx->dbg;
  return true;
}

