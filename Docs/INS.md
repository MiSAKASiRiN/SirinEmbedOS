# INS 服务合同

INS 是 SDK 默认服务，默认采集 BMI088、LSM6DS3、LIS2MDL、SPA06。两个六轴共同驱动一个 Mahony 状态；磁力计与气压计仅发布测量，不参与航向/高度解算。支持用户提供的双 IMU 校准值保存、启动加载与校正；支持手动触发的任意姿态静止陀螺零偏校准；SDK本身不编排六面动作；转台示例与Studio插件实现多姿态加速度/动态陀螺对角比例校准，速度/位置积分尚未实现。温控仅提供恒温加热，不提供传感器温度误差曲线补偿。

应用统一使用本页公共 INS 接口，不选择 legacy 融合或独立温控实现。quaternion EKF 数学组件和外置 OMX IMU CAN 协议适配属于独立能力，不参与本服务。

## 配置和读取

```c
#include "SirinEmbedOS/Kernel/System.h"
#include "SirinEmbedOS/Services/INS.h"

void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *c) {
    c->sensors.heater_enabled = true;      // 默认 true
    c->sensors.heater_target_c = 60.0f;    // 默认 60，允许 20..75 °C
    c->ins.board_to_robot_wxyz[0] = 1;     // 默认单位四元数
    c->ins.board_to_robot_wxyz[1] = 0;
    c->ins.board_to_robot_wxyz[2] = 0;
    c->ins.board_to_robot_wxyz[3] = 0;
}

void controller_cycle(void) {
    SRNOS_INSAttitude a;
    SRNOS_INSGetAttitude(&a);
    if (!(a.flags & SRNOS_INS_VALID)) return; // 应用自行决定失效安全行为
    // 比较 a.sequence 识别新姿态；epoch 改变表示参考航向重新建立。
}
```

架构配置在启动时检查、归一化并冻结。安装四元数必须有限且范数非零，目标温度不能为 NaN/Inf 或超出范围。INS 开启时 BMI/LSM 采样周期均为 1 ms；应用不能运行时改变安装旋转、ODR、温控目标或通过 HostLink 写这些配置。仅作原始采样的 SensorBringUp 设置 `ins.enabled=false`，仍使用同一个 INS/温控 worker；该开关不取消 worker 或静态缓冲，资源变化应以最终 map 和任务创建配置核对。

`SRNOS_INSGetSnapshot` 返回姿态、四路测量与状态的统一副本；`SRNOS_INSGetAttitude` 只复制高频姿态；`SRNOS_INSGetStatus` 在初始化阶段也可调用。均限任务上下文，返回 true 仅表示输出指针有效。多个消费者独立读取，不消费数据、不返回内部指针。复制使用短 IRQ 临界区；`max_copy_us` 包含复制及新鲜度检查的最大耗时，须结合板测判断。

旧数值在失效时保留时间，姿态清 VALID；快照 getter 也检查服务是否超过 5 ms 未更新。姿态序号仅在有效发布时递增，环境测量保持各自序号和采集时间。读取整体一致不代表四个器件同步采样。

## 坐标和单位

板体系与机器人系均为右手 X 前、Y 左、Z 上。水平静止时物理重力为 −Z，加速度计比力为 +Z。固定板内旋转在私有 `Board/Support/Devices/sensor_frames.c` 中实现，应用没有覆盖入口：

| 器件 | 传感器到板体系 |
|---|---|
| BMI088 | `(x,-y,-z)` |
| LSM6DS3、LIS2MDL | `(y,-x,z)` |
| SPA06 | 标量 |

随后应用冻结的 `board_to_robot_wxyz` 将板系向量旋转到机器人系。Sensors API 和 `0x0100/0x0101` 遥测始终保持传感器轴向；INS 的 SI 向量是机器人系，但其中整数原始计数仍保持器件轴向，避免伪造原始数据。

姿态为 Hamilton `[w,x,y,z]`、机器人到世界的主动旋转；世界 Z 向上，初始 yaw=0，仅为相对航向。欧拉角 `[roll,pitch,yaw]` 使用 `Rz(yaw) Ry(pitch) Rx(roll)`，单位 rad；pitch 接近 ±π/2 时欧拉角有奇异性，控制优先使用四元数。四元数在连续运行期间选择与前一发布点积为正的符号；全部六轴失效后重新建立航向会增加 epoch。

加速度单位 m/s²，包含重力对应的比力；角速度 rad/s，磁场 µT，压力 Pa，温度 °C。采集时间是 DWT 主控事务起止时间，不是芯片内部精确采样时间。有效校准记录在启动后应用；参与融合的任一源无有效记录时报告 `UNCALIBRATED`，`RELATIVE_HEADING` 始终保留。gyro-only 校准记录有效不表示完成加速度多姿态标定。

## 输入、调度和启动

独立 INS/温控 worker 为 Realtime / 48、3 KiB、1 ms，只读取传感器快照，不访问总线。BMI 在 DMA 完成通知后由采样线程于当前周期发布。共享 SPI4 dispatcher 为 Realtime1 / 49，BMI/LSM/INS 为 Realtime / 48，CAN4 为 High7 / 47，环境 worker 为 Normal / 24。运行循环不分配内存、不打印、不等待 USB；使用 DWT 实际 dt，每次只推进一次，不追赶补算。

- 两路 READY、加速度和角速度完整且有限、年龄 ≤2 ms、结束时间差 ≤1 ms，按权重合成输入。稳定双路加速度和角速度各等权。
- 对齐不满足时选择最新合格单路，不等待另一颗；来源/权重、输入序号/年龄、TIME_SKEW/SINGLE/PREDICTED 在姿态中公开。
- 无新观测时最多用健康输入的最近角速度预测至 5 ms，不重复修正旧加速度；超过 5 ms 撤销姿态有效性。
- 第二路恢复必须连续健康 100 ms，随后 100 ms 内平滑恢复为等权，不重置四元数。双路、单路、预测、缺样、恢复、周期/执行耗时和截止期错过都有计数。
- Mahony 基础 Kp=1、Ki=0，保留三维重力叉乘。加速度模长 0.9..1.1 g 权重满，0.7/1.3 g 外归零；角速度按现有 5..80°/s 区间衰减。不增加输入滤波或自动零偏校准。

流程为探测 → 温稳等待 → 低动态观测 → 收敛 → 正常/降级。Application、USB 和通信不等待 INS 温稳。

1. 启动 3 秒内必须获得至少一颗有效完整六轴，否则 PWM 归零并 Panic；单颗缺失、磁力计/气压计缺失不阻止 INS 上线。
2. 从首次可用六轴开始等待温稳：当前反馈目标 ±0.25°C 连续 5 秒；关闭加热直接跳过。等待达到 60 秒则继续观测，保留温度未就绪/超时标记，加热继续。
3. 连续 0.5 秒低动态窗口求平均重力：模长 0.9..1.1 g、角速度模长 <5°/s、加速度波动 RMS <0.03 g、角速度波动 RMS <0.5°/s；两者均按三轴分量整体 RMS `sqrt((varX+varY+varZ)/3)` 计算。无合格窗口就等待，不强制发布。
4. 用重力建立初始姿态，继续融合至少 2 秒且末尾至少 0.2 秒低动态，才设置 VALID。
5. 运行温度变化仅更新降级状态，不重置姿态。全部六轴失效立即撤销 VALID/停止加热，传感器各自重试；恢复后增加 epoch 并重新观察收敛。服务超过 5 ms 未执行也重新收敛，避免把长间隔当正常积分。

## 温控

PWM 仅由 INS worker 持有；不再依赖 BMI 的初始化或重试。反馈优先 LSM，失效时用 BMI，LSM 恢复连续健康 100 ms 后切回（BMI 不可用时直接使用有效 LSM）。温度作用于实际来源，不补上推测的两芯片温差。来源变化重置温稳计时，不让已运行的姿态重新等待温稳。

- 反馈低于目标 −10°C：立即 100% PWM，绕过缓升。
- 目标附近：位置 PI 40/2/0，输出 0..500，积分 0..350，普通变化限速 25 输出单位/秒；全功率使用输出跟踪与反积分饱和，进入 PI 不突然跳变。
- 任一健康六轴温度 ≥80°C：立即关 PWM，记录来源位与保护次数；姿态仍可继续。
- 触发过温的器件必须重新有效，并且所有有效六轴均 <75°C 连续 5 秒，才能自动解除。反馈再连续健康 100 ms 后重启调节。HAL/PWM 硬故障保持关闭。
- 全部温度过期、无效或控制时间异常时关 PWM；恢复需连续健康反馈。温度使用 ≤5 ms 的完整六轴样本，不读取旧值加热。

该保护由运行中的任务执行，调试暂停 CPU 时不会继续闭环。过温路径可用主机注入检查，不以人为把实板升至 80°C 作为默认测试方法。长期热场、环境变化和传感器误差需要各自测量，恒温不等于温度误差已校准。

## 构建与遥测

```sh
cmake --preset Debug -B build/INSBringUp -DSIRINEMBEDOS_EXAMPLE=INSBringUp
cmake --build build/INSBringUp
build/hostlink-venv/bin/python Tools/HostLink/imu_telemetry.py \
  --port CDC1_PATH --streams ins mag pressure --duration 600
```

以上命令在 SDK 根目录执行；先按 [主机工具说明](../Tools/HostLink/README.md) 准备 Python 环境，将 `CDC1_PATH` 换成实际 CDC1。加上 `bmi lsm` 可同时订阅原始流；`--json` 输出每秒完整机器可读状态和每十秒采集/接收速率。未就绪时无姿态事件，状态查询仍报告进度。INS 默认自动注册五条流，应用的 HostLink setup 与样本观察回调仍可附加。

INSBringUp 使用正常 API，每秒日志和调试快照，无周期全任务栈扫描，启动音与黄色第一灯 1 Hz 心跳保留。BringUp 关闭加热并观测正式 INS；SensorBringUp 关闭融合而保持统一温控。SDK 默认启用持久化加载与业务参数 SAVE/LOAD；这些观测示例可显式关闭业务参数加载，但 IMU 校准仍由独立 Storage 启动路径加载。校准失败继续采样和融合并标记 UNCALIBRATED/DEGRADED。

逐样本保存可用 `imu_capture.py --port CDC1_PATH --streams bmi lsm ins mag pressure --warmup 120 --duration 600 --output captures/ins-static-01`；监视器终端摘要不等于完整数据。模块分工、接入示例及采集说明见 [INS README](../Source/Services/INS/README.md)，模块变更见 [INS CHANGELOG](../Source/Services/INS/CHANGELOG.md)。

协议、缓存与 CLI 见 [HostLink](HostLink.md)，分层验证与板测限制见 [Validation](Validation.md)。

手动校准命令、拒绝条件、保存与下次启动合同见 [INS 静态校准](../Source/Services/INS/README.md#手动静态校准)。不会在融合循环中自动学习零偏。
