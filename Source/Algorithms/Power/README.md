# 功率约束算法

公共头为 `SirinEmbedOS/Algorithms/Power/PowerControl.h`。调用者持有 `SRNOS_PowerControlConfig` 与 `SRNOS_PowerControlCtx`，初始化后在控制输出生成与 CAN 发送之间调用 `SRNOS_PowerControlUpdate1kHz()`。该函数原地调整 `cmd_inout`，不创建任务、不读取裁判系统，也不自动下发 CAN。

`dt` 使用秒，功率使用 W，能量使用 J，转速使用 rpm；电流反馈和命令的原始量纲必须与各电机的模型系数一致。`motor_count` 不超过 `SRNOS_POWER_CONTROL_MAX_MOTORS`（16）。配置对象须存活到 context 使用结束；启用电机、命令类型、限幅和模型系数均由应用声明。

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

可通过 `SRNOS_PowerControlGetDebug()` 检查预算、估计功率与缩放系数。主机回归：在 SDK 根运行 `sh Tests/Algorithms/Power/run_host_tests.sh`；数值测试不能替代实物功率模型与保护策略验收。
