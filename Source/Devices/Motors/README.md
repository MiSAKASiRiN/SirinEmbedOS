# CAN 电机适配

MP257 M33 目标使用同一组电机协议类，应用可绑定 CAN1、CAN2 或 CAN3。接口和协议字节布局与 H723 共用。[板级通道](../../../Board/MP257/README.md)


C++17 父项目包含 `SirinEmbedOS/Devices/Motors/<型号>/<型号>.hpp`，全部类、状态和装配函数位于 `SRNOS::Devices`，例如 `SRNOS::Devices::DJI_C610 motor{1};`。支持的型号为 C610、C620、GM6020、达妙 DM4310、DM4340 V1.1、DM6006、DM8009P 和 LK8016E。达妙四款使用同一个 DM/DM.hpp 入口。

反馈帧统一为 `SRNOS::Devices::CANFrame`，底层 POD 为 `SRNOS_CANDeviceFrame`；CAN router 使用自有 `SRNOS_CAN*` 公共接口。对象由父项目持有，注册后的生命周期须覆盖路由使用期。父项目明确选择设备、命令、发送周期和使能行为，包含本模块不会自行驱动电机。

反馈解析 `onRxFeedback()`、离线更新 `tick()` 与 `snapshot()` 使用同一短临界区串行访问状态。解析和独立的浮点换算在临界区外，区内只复制状态、读取前值及必要的多圈累计。快照执行一次有界复制并返回 true，无自旋、重试、动态分配或 RTOS 阻塞；`state()` 返回同样的一致快照。不会因高优先级 CAN worker 抢占应用读取而返回半份状态。

当前固定 Cortex-M7 实现保存 BASEPRI，通过 BASEPRI_MAX 临时屏蔽数值优先级 5..15，并恢复调用者原值；不屏蔽 0..4。允许调度前、任务上下文或数值优先级 5..15 的 ISR，禁止从 NMI/Fault 或 0..4 ISR 调用反馈、tick、快照；可嵌套于已有更强临界区。公共头不引入 HAL/RTOS 依赖，目标实现没有锁等待。实际关中断区耗时仍需在板上测量。

`now_ms` 必须来自同一 32 位毫秒时钟，前进间隔小于等于 INT32_MAX。超时保持 `elapsed > timeout`，默认 100 ms；`tick()` 采样较早、但 RX 已发布更新时刻时，不将无符号减法回绕误判为长时间离线。正常毫秒时钟回绕仍有效。达妙电机在注册时确定超时值，其他型号沿用各自的 timeout setter。LK8016E 的命令与模式一起发布；达妙命令先在临界区外编码，再以短临界区发布整帧。

## 达妙电机注册与反馈

四款电机共享 `SRNOS::Devices::DMMotor`。在启动配置阶段为每台电机显式传入型号、驱动器 CAN ID、反馈 SID、驱动器当前模式及实读的 `PMAX / VMAX / TMAX`。三个量程必须为有限正数，单位依次为 rad、rad/s、N·m。对象保存配置副本，所有反馈换算和 MIT 发送都使用该实例的量程。机构的目标角、限速、允许前馈力矩由应用另外约束。

下面是父项目配置回调中可调用的注册片段。示例假设一台 4340 V1.1 已配置 MIT 模式、ID 1、反馈 SID 0x31 及下列量程。

```cpp
#include "SirinEmbedOS/Devices/Motors/DM/DM.hpp"
using namespace SRNOS::Devices;

static DMMotor joint;
int register_joint() {
    const DMConfig config{DMModel::DM4340V11, 1, 0x31, DMMode::MIT,
                          {12.5f, 10.f, 28.f}};
    return joint.registerCan(SRNOS_CAN_CH_NTFDCAN1, config, 2);
}
```

`registerCan` 和 `unregisterCan` 限启动配置阶段。发送周期和离线超时以毫秒为单位，均须大于零。注册成功返回 0，失败返回负数，失败后可修正配置重试。同一总线上共用反馈 SID 时，各电机 CAN ID 的低四位须可区分。注册后由应用设置工作指令并显式调用 `requestEnable()`。首次设置工作指令前，周期回调不发帧。模式由驱动器固件支持并预先选定，注册不写驱动器寄存器。

| 注册模式 | 设置指令 | CAN 载荷 |
|---|---|---|
| MIT | `setMIT(position_rad, velocity_rad_s, kp, kd, torque_nm)` | 位置 16 位、其余字段 12 位。按实例量程编码，Kp 限 0～500、Kd 限 0～5 |
| PosVel | `setPosVel(position_rad, max_velocity_rad_s)` | 两个小端 float，最大速度取非负值 |
| Velocity | `setVelocity(velocity_rad_s)` | 有符号速度，小端 float，4 字节 |
| ForcePosition | `setForcePosition(position_rad, max_velocity_rad_s, current_fraction)` | 位置 float，速度限 0～100 rad/s，电流比例限 0～1。仅用于支持 0x300 模式的驱动器固件 |

命令 setter 返回 `false` 表示未注册、模式不符、输入非有限或报文与管理指令碰撞等，上一条有效命令继续生效。使能和失能使用紧急队列，Router 接受对应请求后才清除待发标记。标准工作模式与编码量程应与该电机保存的设置一致。型号选择和资料可查[达妙官方选型表](https://raw.githubusercontent.com/dmBots/dmBot/master/2026-Damiao%20Technology-%20Selection%20Guide-2026-10-28.pdf)。

`joint.feedback()` 返回一致的 `DMFeedback`。现场诊断可读无符号原始码、状态高四位及两路温度；控制可读 `position_rad`、`velocity_rad_s`、`torque_nm`、`online` 和 `last_rx_ms`。映射角度保留原始区间，`continuous_position_rad` 累计正反向跨界后的连续角度，单位 rad。默认按完整映射区间 `2 × PMAX` 处理回绕；若实机反馈在每个机械单圈回绕，可在注册时指定 `feedback_period_rad = 6.2831853f`。连续两帧实际位移必须小于半个回绕周期，反馈饱和或中间漏掉整圈时无法从这两帧恢复圈数。首帧和超时后首帧以当前映射角为新基准。默认离线超时 100 ms。

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
