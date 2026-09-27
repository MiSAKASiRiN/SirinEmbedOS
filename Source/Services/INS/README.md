# INS 服务

SirinEmbedOS 固定板载惯性服务：统一坐标，使用 BMI088 与 LSM6DS3 的六轴数据驱动一个 Mahony 姿态状态，并管理唯一的 IMU 加热通路。LIS2MDL 与 SPA06 只提供测量，不参与融合。默认启用，应用无需自行创建 INS 线程。

完整参数、时序与失效合同见 [Docs/INS.md](../../../Docs/INS.md)，本模块历史见 [CHANGELOG](CHANGELOG.md)。

## 实现分工

| 文件 | 职责 |
|---|---|
| `ins_core.c` | 输入选择、双路权重、Mahony、启动/降级/恢复状态机；不访问 HAL/RTOS |
| `ins_heater.cpp` | 温度反馈选择、PI、温稳与过温保护状态 |
| `ins_service.c` | High5、3 KiB、1 ms worker，读取采样快照、执行温控、发布结果 |
| `ins_snapshot.c` | 多任务一致复制、有效性检查与复制耗时统计 |
| `ins_internal.h` | 模块内部状态及接线，业务代码不包含此头 |

公共 API 位于 [INS.h](../../../Include/SirinEmbedOS/Services/INS.h)。固定器件旋转由 [Board glue](../../../Board/Glue/sensor_frames.c) 实现；驱动与 SPI/I²C 所有权仍在 Devices/IO，遥测编码位于 [ins_telemetry.c](../HostLink/ins_telemetry.c)。INS 不操作总线，不在周期循环内分配内存或等待 USB。

## 应用接入

```c
#include "SirinEmbedOS/Kernel/System.h"
#include "SirinEmbedOS/Services/INS.h"

void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *c)
{
    c->ins.enabled = true;
    c->ins.board_to_robot_wxyz[0] = 1.0f;
    c->ins.board_to_robot_wxyz[1] = 0.0f;
    c->ins.board_to_robot_wxyz[2] = 0.0f;
    c->ins.board_to_robot_wxyz[3] = 0.0f;
    c->sensors.heater_enabled = true;
    c->sensors.heater_target_c = 60.0f;
}

/* 在应用自己的控制周期调用；失效时应用执行自己的安全策略。 */
void control_cycle(void)
{
    SRNOS_INSAttitude a;
    if (!SRNOS_INSGetAttitude(&a) || !(a.flags & SRNOS_INS_VALID))
        return;
    /* 用 sequence 判新样本；epoch 改变时重新处理相对航向参考。 */
}
```

这些也是默认值。配置在启动时检查并冻结：安装四元数必须有限且非零，SDK 归一化；加热目标允许 20～75°C。不能通过 HostLink 或运行期重绑修改配置。

- `SRNOS_INSGetAttitude()`：小型高频姿态副本。
- `SRNOS_INSGetSnapshot()`：姿态、四颗传感器最新测量与状态的一致副本。
- `SRNOS_INSGetStatus()`：包含初始化阶段、健康、温控和运行计数。

三个 getter 限任务上下文，返回值只表示输出指针有效；姿态可用性检查 `SRNOS_INS_VALID`。多个任务可独立读取，不消费数据、不共享可变指针。失效时保留旧数值及时间、清有效位，不能只凭非零四元数判断已就绪。

## 坐标与单位

板体系为右手 X 前、Y 左、Z 上；水平静止时比力指向 +Z，物理重力指向 −Z。器件到板的旋转固定为 BMI `(x,-y,-z)`、LSM/LIS `(y,-x,z)`，应用只能配置其后的板到机器人安装旋转。

姿态采用 Hamilton `w,x,y,z`，表示机器人到世界的旋转，世界 Z 向上。欧拉角按 `Rz(yaw) Ry(pitch) Rx(roll)`，单位 rad；俯仰接近 ±π/2 时优先使用四元数。yaw 初始为零，仅是相对航向。

INS 的 SI 向量为机器人坐标：加速度 m/s²（包含重力对应的比力）、角速度 rad/s、磁场 µT；压力 Pa、温度 °C。整数原始计数及独立 Sensors API / 双 IMU 原始遥测仍为传感器轴。一致快照不表示传感器同步采样；DWT 时间是主控采集/计算时间。

## 就绪、掉线与温控

启动依次经过探测、温稳、低动态观测、收敛，最后进入正常或降级运行。应用线程和 CDC 不等待温稳；订阅姿态后仍可能暂时没有姿态样本，应读取状态。

- 启动 3 秒内没有任何完整有效六轴样本：停止加热并 Panic。单颗六轴可上线，磁力计/气压计缺失不阻塞。
- 双路年龄 ≤2 ms、结束时间差 ≤1 ms 时合成输入；稳定双路等权，否则选合格单路。短暂缺样最多预测到 5 ms，之后撤销有效性。
- 全部六轴失效后停止加热并继续重试；恢复重新观察/收敛，增加姿态 epoch。第二路恢复经过健康确认和平滑加权，不重置运行中的姿态。
- 默认加热 60°C，LSM 优先、BMI 回退；低于目标 10°C 时满功率，附近采用 PI 与输出限速。目标 ±0.25°C 连续 5 秒视为温稳，等待 60 秒仍未温稳则带标记继续启动。
- 低动态观测连续 0.5 秒后建立重力方向，继续融合至少 2 秒且末尾 0.2 秒低动态才发布有效姿态；持续运动可使初始化继续等待。
- 任一健康六轴温度 ≥80°C 立即关 PWM；满足触发来源重新有效且全部有效温度 <75°C 连续 5 秒后才可恢复。硬件故障保持关闭。

本阶段“温补”只是恒温，不含温度误差曲线、用户零偏/比例/安装标定。默认禁用旧参数加载及 HostLink 存储命令，未适配 EEPROM。没有 legacy 融合或温控选择入口。

## CDC 测试与保存

使用 **INSBringUp 或启用正式 INS 的固件**；SensorBringUp 关闭融合，因此不会产生姿态流。CDC0 输出文本日志，**CDC1** 承载 COBS / SRN-Link V2 二进制协议。普通串口文本终端不能直接解读。

在 SDK 根目录执行（将 `CDC1_PATH` 换成实际 CDC1，目录必须不存在）：

```sh
python3 -m venv build/hostlink-venv
build/hostlink-venv/bin/pip install -r Tools/HostLink/requirements.txt
build/hostlink-venv/bin/python -m serial.tools.list_ports -v

# 先观察启动、温度、姿态；此命令完成后再进行采集。
build/hostlink-venv/bin/python Tools/HostLink/imu_telemetry.py \
  --port CDC1_PATH --streams bmi lsm ins mag pressure --duration 10

# 五路完整逐样本保存：预热记录 120 秒，随后测量 600 秒。
build/hostlink-venv/bin/python Tools/HostLink/imu_capture.py \
  --port CDC1_PATH --streams bmi lsm ins mag pressure \
  --warmup 120 --duration 600 --output captures/ins-static-01 \
  --note "板子固定静置，默认60°C，记录实际振动与安装方向"
```

已完成温稳/收敛时可用 `--warmup 0`；只测姿态可用 `--streams ins`。预热选项只标记时间段，不控制加热，也不保证 INS 已就绪；确认状态进入运行/降级并有有效姿态。一次只运行一个串口客户端。

保存 `bmi088.csv`、`lsm6ds3.csv`、`ins.csv`、`lis2mdl.csv`、`spa06.csv`（仅所选流），另有原始 `cdc1_rx.bin`、每秒 `status.jsonl` 与 `metadata.json`。姿态 CSV 包含四元数、欧拉角、来源/权重、输入年龄、epoch、计算/发布时间等完整字段；温控状态和反馈温度保存在状态文件，双 IMU CSV 保存各自温度。

采集完整性检查 `metadata.json` 的 `complete`、`quality_issues`、各流 `measurement_rows` / `measurement_rate_hz`，并核对设备溢出/失败计数。分析选 `phase=measurement`；姿态用 `attitude_us`、原始测量用 `acquisition_start_us`，不使用 USB 批次到达时间当采样时间。epoch 改变后不能直接当成同一航向参考分析。

保存工具会拒绝覆盖旧目录；Ctrl-C 保存并标记 interrupted；断连保存错误，不自动拼接复位前后会话。未就绪或离线不会伪造样本。监视器的终端摘要或重定向不能替代逐样本保存。详细字段和异常收尾见 [采集工具说明](../../../Tools/HostLink/README.md)。

## 验证边界

主机测试在 [Tests/Services/INS](../../../Tests/Services/INS) 和 [IMUTelemetry](../../../Tests/Services/IMUTelemetry)。固件构建、板测证据与未测事项以 [Validation](../../../Docs/Validation.md) 为准。2026-09-28 已完成三个 10 秒窗口短测，不能据此宣称十分钟稳定性、精度校准、动态性能或满 CAN 负载验收通过。
