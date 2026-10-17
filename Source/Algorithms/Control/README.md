# 控制算法

父项目包含 `SirinEmbedOS/Algorithms/Control/pid.hpp` 或 `control_extensions.hpp`，通过 `SRNOS::Control` 使用 PID 及扩展控制算法；数学组件位于 `SRNOS::Math`。接口不创建线程或访问硬件。

控制器初始化一次，在应用任务每个 step 中调用 `step()`，传入实际秒数 `dt_s`。业务负责反馈新鲜度、单位、输出限幅和执行器使能。参数必须通过 `configure()` 校验；`PidResult.status` 非 `Ok` 时不能按正常控制输出处理。

```cpp
#include "SirinEmbedOS/Algorithms/Control/pid.hpp"

static SRNOS::Control::Pid speed_pid;
bool configure_speed_pid() {
    SRNOS::Control::PidConfig config{};
    config.gains.kp = 0.2f; // 仅示意，实际增益和量纲由应用整定
    config.output_min = -1.0f;
    config.output_max = 1.0f;
    return speed_pid.configure(config) == SRNOS::Control::PidStatus::Ok;
}
SRNOS::Control::PidResult calculate(float target, float measured, float dt_s) {
    return speed_pid.step({target, measured, 0.0f, dt_s});
}
```

以上片段只计算，不下发电机。主机回归：在 SDK 根运行 `sh Tests/Algorithms/Control/run_host_tests.sh`；控制稳定性和板端耗时需要由具体应用验收。
