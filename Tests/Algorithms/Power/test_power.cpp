#include "SirinEmbedOS/Algorithms/Power/PowerControl.h"
#include <cassert>
#include <cmath>

int main()
{
    SRNOS_PowerControlConfig cfg{};
    cfg.motor_count = 2;
    cfg.q_joule = 60;
    cfg.z_min_joule = 10;
    cfg.budget_filter_alpha = 0;
    cfg.motors[0].enabled = true;
    cfg.motors[0].k_i2 = 1;
    cfg.motors[0].cmd_limit = 100;
    SRNOS_PowerMotorMeasurement meas[2]{};
    int16_t cmd[2] = {10, 42};
    SRNOS_PowerControlCtx ctx{};
    SRNOS_PowerControlInit(&ctx, &cfg);
    SRNOS_PowerControlUpdate1kHz(&ctx, 0.001f, true, 50, 0, 0, false, meas, cmd, 2);
    SRNOS_PowerControlDebug debug{};
    assert(SRNOS_PowerControlGetDebug(&ctx, &debug));
    assert(debug.P_total_est == 100);
    assert(debug.P_budget == 50);
    assert(debug.P_total_after == 25);
    assert(cmd[0] == 5 && cmd[1] == 42);

    cfg.motors[0].slew_rate = 1000; // command units/s
    cmd[0] = -100;
    SRNOS_PowerControlUpdate1kHz(&ctx, 0.001f, false, 10000, 0, 0, false, meas, cmd, 2);
    assert(cmd[0] == 4); // preserve the 1 unit/cycle slew bound
    assert(!ctx.online);
    SRNOS_PowerControlReset(&ctx);
    assert(ctx.last_cmd[0] == 0 && ctx.dbg.scale_all == 1);
    assert(!SRNOS_PowerControlGetDebug(nullptr, &debug));
}
