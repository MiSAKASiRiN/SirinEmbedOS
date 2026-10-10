# CAN 电机适配

C++17 父项目包含 `SirinEmbedOS/Devices/Motors/<型号>/<型号>.hpp`，全部类、状态和装配函数位于 `SRNOS::Devices`，例如 `SRNOS::Devices::DJI_C610 motor{1};`。支持的型号为 C610、C620、GM6020、DM4310、DM4340、LK8016E。

反馈帧统一为 `SRNOS::Devices::CANFrame`，底层 POD 为 `SRNOS_CANDeviceFrame`；CAN router 使用自有 `SRNOS_CAN*` 公共接口。对象由父项目持有，注册后的生命周期须覆盖路由使用期。父项目明确选择设备、命令、发送周期和使能行为，包含本模块不会自行驱动电机。

反馈解析 `onRxFeedback()`、离线更新 `tick()` 与 `snapshot()` 使用同一短临界区串行访问状态。解析和独立的浮点换算在临界区外，区内只复制状态、读取前值及必要的多圈累计。快照执行一次有界复制并返回 true，无自旋、重试、动态分配或 RTOS 阻塞；`state()` 返回同样的一致快照。不会因高优先级 CAN worker 抢占应用读取而返回半份状态。

当前固定 Cortex-M7 实现保存 BASEPRI，通过 BASEPRI_MAX 临时屏蔽数值优先级 5..15，并恢复调用者原值；不屏蔽 0..4。允许调度前、任务上下文或数值优先级 5..15 的 ISR，禁止从 NMI/Fault 或 0..4 ISR 调用反馈、tick、快照；可嵌套于已有更强临界区。公共头不引入 HAL/RTOS 依赖，目标实现没有锁等待。实际关中断区耗时仍需在板上测量。

`now_ms` 必须来自同一 32 位毫秒时钟，前进间隔小于等于 INT32_MAX。超时保持 `elapsed > timeout`，默认 100 ms；`tick()` 采样较早、但 RX 已发布更新时刻时，不将无符号减法回绕误判为长时间离线。正常毫秒时钟回绕仍有效。超时值通过原子读写更新；LK8016E 电流满量程配置也为原子单值。控制命令的现有单字段原子接口保持，组合命令的多个独立字段不因此获得事务性。

协议字段、缩放、发送周期与使能语义保持；公共函数签名不变，类的私有布局有调整，父项目需重新编译。`Tests/Devices/Motors/run_host_tests.sh` 覆盖六型号的旧时间、超时边界、时钟回绕、无效帧、嵌套 mask，以及 RX/tick/多读者并发一致性；默认 UBSan，可用 `SRNOS_MOTOR_SANITIZERS=thread` 运行 ThreadSanitizer。主机使用独立寄存器替身以 mutex 模拟单核临界区，不证明板端中断延迟或电机控制行为。
