#include "SirinEmbedOS/Algorithms/Power/lib_power_control.h"
#include <cassert>
#include <cmath>

int main()
{
    power_control_config_t cfg{};
    cfg.motor_count = 2;
    cfg.q_joule = 60;
    cfg.z_min_joule = 10;
    cfg.budget_filter_alpha = 0;
    cfg.motors[0].enabled = true;
    cfg.motors[0].k_i2 = 1;
    cfg.motors[0].cmd_limit = 100;
    motor_meas_t meas[2]{};
    int16_t cmd[2] = {10, 42};
    power_control_ctx_t ctx{};
    PowerControl_Init(&ctx, &cfg);
    PowerControl_Update1kHz(&ctx, 0.001f, true, 50, 0, 0, false, meas, cmd, 2);
    power_control_debug_t debug{};
    assert(PowerControl_GetDebug(&ctx, &debug));
    assert(debug.P_total_est == 100);
    assert(debug.P_budget == 50);
    assert(debug.P_total_after == 25);
    assert(cmd[0] == 5 && cmd[1] == 42);

    cfg.motors[0].slew_rate = 1000; // command units/s
    cmd[0] = -100;
    PowerControl_Update1kHz(&ctx, 0.001f, false, 10000, 0, 0, false, meas, cmd, 2);
    assert(cmd[0] == 4); // preserve the 1 unit/cycle slew bound
    assert(!ctx.online);
    PowerControl_Reset(&ctx);
    assert(ctx.last_cmd[0] == 0 && ctx.dbg.scale_all == 1);
    assert(!PowerControl_GetDebug(nullptr, &debug));
}
