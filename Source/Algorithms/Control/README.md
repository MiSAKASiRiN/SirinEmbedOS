# 控制算法

父项目包含 `SirinEmbedOS/Algorithms/Control/pid.hpp` 或 `control_extensions.hpp`，通过 `SRNOS::Control` 使用 PID 及扩展控制算法；数学组件位于 `SRNOS::Math`。接口不创建线程或直接调用 RTOS。

业务决定调用周期、反馈有效性和执行器输出，在托管任务的单轮 step 中调用。迁移后不保留全局旧 namespace 别名，计算顺序与数值合同保持。

验证命令：`sh Tests/Algorithms/Control/run_host_tests.sh`；实机控制稳定性由具体应用验收。
