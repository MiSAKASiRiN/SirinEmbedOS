# CAN 电机适配

C++17 父项目包含 `SirinEmbedOS/Devices/Motors/<型号>/<型号>.hpp`，全部类、状态和装配函数位于 `SRNOS::Devices`，例如 `SRNOS::Devices::DJI_C610 motor{1};`。支持的型号为 C610、C620、GM6020、DM4310、DM4340、LK8016E。

反馈帧统一为 `SRNOS::Devices::CANFrame`，底层 POD 为 `SRNOS_CANDeviceFrame`；CAN router 使用自有 `SRNOS_CAN*` 公共接口。对象由父项目持有，注册后的生命周期须覆盖路由使用期。父项目明确选择设备、命令、发送周期和使能行为，包含本模块不会自行驱动电机。

反馈解析 `onRxFeedback()`、离线更新 `tick()` 与 `snapshot()` 使用同一短临界区串行访问状态。解析和独立的浮点换算在临界区外，区内只复制状态、读取前值及必要的多圈累计。快照执行一次有界复制并返回 true，无自旋、重试、动态分配或 RTOS 阻塞；`state()` 返回同样的一致快照。不会因高优先级 CAN worker 抢占应用读取而返回半份状态。

当前固定 Cortex-M7 实现保存 BASEPRI，通过 BASEPRI_MAX 临时屏蔽数值优先级 5..15，并恢复调用者原值；不屏蔽 0..4。允许调度前、任务上下文或数值优先级 5..15 的 ISR，禁止从 NMI/Fault 或 0..4 ISR 调用反馈、tick、快照；可嵌套于已有更强临界区。公共头不引入 HAL/RTOS 依赖，目标实现没有锁等待。实际关中断区耗时仍需在板上测量。

`now_ms` 必须来自同一 32 位毫秒时钟，前进间隔小于等于 INT32_MAX。超时保持 `elapsed > timeout`，默认 100 ms；`tick()` 采样较早、但 RX 已发布更新时刻时，不将无符号减法回绕误判为长时间离线。正常毫秒时钟回绕仍有效。超时值通过原子读写更新；LK8016E 电流满量程配置也为原子单值。DM4310、DM4340、LK8016E 的每次组合命令 setter 使用同一短临界区发布全部字段，TX 在短临界区复制包含 mode 的完整命令快照后，再在区外编码；DM4340 反馈解码也一致读取三项 MIT 范围。LK8016E 的命令值与该 setter 选择的 mode 同时发布。单字段 setter 保持单次更新，连续调用不同 setter 或先 set_mode 再设置命令不构成跨调用事务；应用应优先使用已有组合 setter。命令发布和导出遵守相同任务/IRQ 5..15 上下文限制，无自旋和运行期分配；使能请求、线协议及命令范围保持原合同。

## C620 接入示例

以下片段在配置阶段注册到 CAN1，每 2 ms 输出一次当前命令，初始命令为零。必须检查注册结果；CAN1 应配置为 Classic，同组电机的发送由 Router 合并。

```cpp
#include "SirinEmbedOS/Devices/Motors/C620/C620.hpp"
static SRNOS::Devices::DJI_C620 motor{1};

int register_motor() { // 由 ApplicationConfigure 调用，不放入全局构造函数
    motor.set_give_cmd(0);
    return SRNOS::Devices::DJI_C620_AttachCan(1, &motor, 2);
}
bool read_motor(SRNOS::Devices::C620State& out) {
    return motor.snapshot(out);
}
```

注册后由 Router 调用反馈、tick 与 poll_tx。业务在自身控制周期检查在线状态、反馈年龄和限幅后，才更新 `set_give_cmd()`；原始命令不等于转速设定值。不同型号的使能/命令语义以对应公共头为准。

模型以 mutex 模拟单核临界区，不能证明板端中断延迟或电机控制行为。
