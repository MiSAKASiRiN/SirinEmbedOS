# 功率约束算法

公共头为 `SirinEmbedOS/Algorithms/Power/PowerControl.h`。调用者持有 `SRNOS_PowerControlConfig` 与 `SRNOS_PowerControlCtx`，初始化后在控制输出生成与 CAN 发送之间调用 `SRNOS_PowerControlUpdate1kHz()`。该函数原地调整 `cmd_inout`，不创建任务、不读取裁判系统，也不自动下发 CAN。

`dt` 使用秒，功率使用 W，能量使用 J，转速使用 rpm；电流反馈和命令的原始量纲必须与各电机的模型系数一致。`motor_count` 不超过 `SRNOS_POWER_CONTROL_MAX_MOTORS`（16）。配置对象须存活到 context 使用结束；启用电机、优先级、限幅和模型系数均由应用声明。配置与 context 由应用单一任务持有，并发调用须由应用同步。

`P_meas_valid=true` 且实测功率有限、非负时，使用实测总功率判断预算，并按模型中关键组/普通组的占比分配该总量，再计算缩放。没有可用模型占比时对所有启用电机统一缩放；无有效实测值时使用模型总量。非有限 `dt` 或不在 `(0,0.1]` 秒内时沿用 1 ms 回退值。

输出先按命令限幅及变化率处理，再估算模型并施加最终功率缩放；命令回退估算使用这一步处理后的命令；`last_cmd` 保存最终输出。因此降额可快于配置的变化率，避免变化率限制抬高已经降额的命令。优先级仍先限制普通组，关键组本身超预算时全体统一缩放。该算法是经应用标定的命令缩放，不构成实测功率硬上限。

以下为接入片段，`config` 应由项目提供并完成模型校准，测量数组与命令数组至少包含 `config.motor_count` 项：

```c
#include "SirinEmbedOS/Algorithms/Power/PowerControl.h"

void apply_power_limit(SRNOS_PowerControlCtx *ctx, float dt_s,
                       const SRNOS_PowerMotorMeasurement *meas,
                       int16_t *commands, uint32_t count) {
    // 示例：预算源离线，使用配置中的 fallback；不伪造有效实测功率。
    SRNOS_PowerControlUpdate1kHz(ctx, dt_s, false, 0.0f, 0.0f,
                               0.0f, false, meas, commands, count);
}
// 在初始化阶段调用 SRNOS_PowerControlInit(&ctx, &config)，每轮再调用上式。
```

可通过 `SRNOS_PowerControlGetDebug()` 检查预算、估计功率与缩放系数。`P_crit/P_non` 在有效实测与模型占比可用时表示分配后的实测功率；其他情况保留模型分组值。`P_total_after` 是现有模型按所选电流来源计算的诊断估计；使用反馈电流时不能据此推断新命令已在设备上生效。
