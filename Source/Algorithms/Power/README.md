# 功率约束算法

父项目包含 `SirinEmbedOS/Algorithms/Power/PowerControl.h`，使用 `SRNOS_PowerControlConfig`、`SRNOS_PowerControlCtx` 和 `SRNOS_PowerMotorMeasurement`。初始化后，在自己的控制步骤中通过 `SRNOS_PowerControlUpdate1kHz` 提交实际 dt、功率预算、测量和电机命令。

这是由业务显式调用的数值算法，不创建 SDK 线程、不自动读取裁判系统或下发 CAN。所有公共类型、函数和宏使用 SRNOS 名称，不再提供旧测量结构别名。

验证命令：`sh Tests/Algorithms/Power/run_host_tests.sh`；功率模型和保护策略的实物适用性仍由父项目验证。
