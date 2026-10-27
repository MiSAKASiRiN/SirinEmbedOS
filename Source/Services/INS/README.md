# INS 服务

SirinEmbedOS 固定板载惯性服务：统一坐标，使用 BMI088 与 LSM6DS3 的六轴数据驱动一个 Mahony 姿态状态，并管理唯一的 IMU 加热通路。LIS2MDL 与 SPA06 只提供测量，不参与融合。默认启用，应用无需自行创建 INS 线程。

完整参数、时序与失效合同见 [Docs/INS.md](../../../Docs/INS.md)，本模块历史见 [CHANGELOG](CHANGELOG.md)。

## 实现分工

| 文件 | 职责 |
|---|---|
| `ins_core.c` | 输入选择、双路权重、Mahony、启动/降级/恢复状态机；不访问 HAL/RTOS |
| `ins_heater.cpp` | 温度反馈选择、PI、温稳与过温保护状态 |
| `ins_service.c` | P0 / Realtime（48）、3 KiB、1 ms worker，读取采样快照、执行温控、发布结果 |
| `ins_snapshot.c` | 多任务一致复制、有效性检查与复制耗时统计 |
| `ins_internal.h` | 模块内部状态及接线，业务代码不包含此头 |

公共 API 位于 [INS.h](../../../Include/SirinEmbedOS/Services/INS.h)。固定器件旋转由 [Board glue](../../../Board/Support/Devices/sensor_frames.c) 实现；驱动与 SPI/I²C 所有权仍在 Devices/IO，遥测编码位于 [ins_telemetry.c](../SirinLink/ins_telemetry.c)。INS 不操作总线，不在周期循环内分配内存或等待 USB。

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

`ins.enabled=false` 关闭融合，不等于取消 INS worker、传感器 worker 或相关静态缓冲；传感器和加热选择分别由 `sensors` 配置管理，资源裁剪须核对最终 map。

这些也是默认值。配置在启动时检查并冻结：安装四元数必须有限且非零，SDK 归一化；加热目标允许 20～75°C。不能通过 SirinLink 或运行期重绑修改配置。

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

温控只提供恒温，不含温度误差曲线补偿。EEPROM 校准记录加载/保存和 SirinLink 存储默认启用；零偏/比例校正及静态校准的当前边界见下文。没有 legacy 融合或温控选择入口。

## CDC 采集与保存

使用启用了正式 INS 的应用固件；关闭融合时不会产生姿态流。CDC0 输出文本日志，**CDC1** 承载 COBS / SRN-Link V2 二进制协议。普通串口文本终端不能直接解读。

在 SDK 根目录执行（将 `CDC1_PATH` 换成实际 CDC1，目录必须不存在）：

```sh
python3 -m venv build/sirinlink-venv
build/sirinlink-venv/bin/pip install -r Tools/SirinLink/requirements.txt
build/sirinlink-venv/bin/python -m serial.tools.list_ports -v

# 先观察启动、温度、姿态；此命令完成后再进行采集。
build/sirinlink-venv/bin/python Tools/SirinLink/imu_telemetry.py \
  --port CDC1_PATH --streams bmi lsm ins mag pressure --duration 10

# 五路完整逐样本保存：预热记录 120 秒，随后测量 600 秒。
build/sirinlink-venv/bin/python Tools/SirinLink/imu_capture.py \
  --port CDC1_PATH --streams bmi lsm ins mag pressure \
  --warmup 120 --duration 600 --output captures/ins-static-01 \
  --note "板子固定静置，默认60°C，记录实际振动与安装方向"
```

已完成温稳/收敛时可用 `--warmup 0`；只测姿态可用 `--streams ins`。预热选项只标记时间段，不控制加热，也不保证 INS 已就绪；确认状态进入运行/降级并有有效姿态。一次只运行一个串口客户端。

保存 `bmi088.csv`、`lsm6ds3.csv`、`ins.csv`、`lis2mdl.csv`、`spa06.csv`（仅所选流），另有原始 `cdc1_rx.bin`、每秒 `status.jsonl` 与 `metadata.json`。姿态 CSV 包含四元数、欧拉角、来源/权重、输入年龄、epoch、计算/发布时间等完整字段；温控状态和反馈温度保存在状态文件，双 IMU CSV 保存各自温度。

采集完整性检查 `metadata.json` 的 `complete`、`quality_issues`、各流 `measurement_rows` / `measurement_rate_hz`，并核对设备溢出/失败计数。分析选 `phase=measurement`；姿态用 `attitude_us`、原始测量用 `acquisition_start_us`，不使用 USB 批次到达时间当采样时间。epoch 改变后不能直接当成同一航向参考分析。

保存工具会拒绝覆盖旧目录；Ctrl-C 保存并标记 interrupted；断连保存错误，不自动拼接复位前后会话。未就绪或离线不会伪造样本。监视器的终端摘要或重定向不能替代逐样本保存。详细字段和异常收尾见 [采集工具说明](../../../Tools/SirinLink/README.md)。

## 板级边界

姿态精度、动态性能和负载时序需使用目标固件分别测量；构建通过不代表这些指标已验收。

传感器固定坐标及加热 PWM 由 Board/Support/Devices 提供，heater_board 负责输出；本服务负责融合、温控和状态。

## 托管线程

SRNOS_INS 使用托管单轮与 1 ms 超期重置等待；初始化不纳入循环统计。INS 为 P0 / Realtime（48）、3 KiB 栈，与 BMI/LSM 和 CAN1/2/3 同级。私有等待适配由 Kernel 驱动；普通等待不计入 step，处理期间 I/O/抢占/中断计入墙钟。统计不能替代板端时序验收。详见根文档及 Source/Kernel/README.md。

## 遥测快照边界

INS 与原始传感器遥测逐条在短保护区取得记录，编码在保护外完成，避免整批复制期间屏蔽中断。队列容量、线记录格式、订阅合同和硬件采样频率保留；HostComm 的拥塞/断连不反向改变融合周期。临界区和 INS 时序应按目标固件及实际负载测量。

## 校准持久化

公共接口为 `SirinEmbedOS/Services/IMUCalibration.h`：Validate、Get、GetStatus、Save。BMI088 和 LSM6DS3 各一份记录，SENSOR 轴、SI 单位，v1 保存三轴 gyro bias、accel bias/对角 scale 及校准温度。公式为 `g'=g-bias`、`a'=(a-bias)*scale`，在板级和应用旋转前应用。Sensors 的原始数据及原始遥测保持不变。支持上位机触发任意姿态静止陀螺零偏校准；加速度 bias/scale 仍须由外部多姿态校准流程提供。

校验有限数值、器件/模型/坐标版本；gyro bias各轴绝对值≤1 rad/s、accel bias≤9.80665 m/s²、scale 0.5–1.5、温度−40～125°C。它们是拒绝异常记录的工程边界，不是准确性保证。Save在调用任务协作等待（0表示默认5s），不得从控制/INS周期调用；固定快照写入后只标记pending_restart，新值下次启动才生效，失败不改变活动校准。

AppInit负责 EEPROM，INS仅等待解析结果并继续温控/状态更新。两颗校准分别校验，总启动存储预算2s；超时关闭接纳窗口，迟到结果不能进入融合。无效记录按零偏0、scale1回退，参与融合的任一源未校准时设置UNCALIBRATED并进入DEGRADED，姿态仍可VALID。真实传感器健康与存储状态分开；默认没有校准的板子以后会明确显示降级。

`SRNOS_IMUCalibrationGetStatus()`提供各器件加载/保存结果、活动/保存revision及待重启掩码；`INSStatus.calibrated_imu_mask`和`calibration_load_pending`给出融合侧状态。身份、校准和参数互不覆盖，单份写入不保证保留旧版本。序列化、独立损坏、时钟回绕/迟到加载、重启生效和非单位安装旋转可通过主机回归检查；实际校准精度、断电以及共享总线压力需板测。

保存由外部校准流程取得的数据（仅在低优先级管理任务调用）：

```c
#include "SirinEmbedOS/Services/IMUCalibration.h"
SRNOS_StorageStatus save_measured_calibration(const SRNOS_IMUCalibration *measured) {
    SRNOS_StorageStatus result = SRNOS_IMUCalibrationValidate(measured);
    if (result != SRNOS_STORAGE_OK) return result;
    return SRNOS_IMUCalibrationSave(measured, 5000);
}
/* measured必须填入sensor、MODEL_VERSION、SENSOR_AXES_VERSION和实测SI数值；
 * 成功后Get仍返回当前活动值，用GetStatus检查pending_restart_mask。 */
```

## 手动静态校准

上位机在 CONFIG service `0x02` 发 REQUEST message `0x09`，正文 `01 03` 表示 v1、两颗 IMU；`01 01` 仅 BMI088，`01 02` 仅 LSM6DS3。无需六面放置，但整个观察窗口必须物理静止，建议温度稳定后触发。先固定板卡再发命令，不应在运动控制运行时校准。协议和完整结果字段见 [SirinLink 协议](../../../Docs/SirinLink.md#手动静态-imu-校准)。

HostCmd 每 10 ms 读取一次最新 SENSOR 轴原始快照，收集约 5 s，估计三轴陀螺零偏和平均温度。单个未知姿态不能把重力与加速度零偏分开，因此保留 EEPROM 中有效的 accel bias/scale；无有效记录时使用 bias=0、scale=1。不会把当前重力向量作为加速度零偏扣除。IMU 也无法区分足够缓慢的匀速转动与零偏，软件静止检查不能替代实际静止要求。[ST 加速度校准说明](https://www.st.com/resource/en/application_note/an4508-parameters-and-calibration-of-a-lowg-3axis-accelerometer-stmicroelectronics.pdf)

拒绝条件包括：传感器非 READY、字段无效、错误/丢样/恢复计数变化、非有限值、样本年龄超过 20 ms、相邻观察时间超过 40 ms 或序号不前进；加热故障、过温和缺失反馈也会结束操作。观察窗口还要求至少 400 个不同样本、跨度至少 4.9 s。工程静止阈值为原始单轴角速度绝对值≤0.10 rad/s、峰峰值≤0.12 rad/s、标准差≤0.02 rad/s；加速度模长 8.8–10.8 m/s²、单轴峰峰值≤0.40 m/s²、标准差≤0.08 m/s²；温度变化≤1.0°C。以上为异常拒绝条件；这些阈值对轻微运动可能不敏感，不能代替物理静止要求或校准精度证明。

所有选中 IMU 都通过采集校验后才开始保存。采集及保存过程持有校准写入租约，其他本地校准 Save/RunStatic 返回 BUSY；EEPROM 总线只在真正读写时占用，普通参数存储仍可能竞争并使本操作报错。每颗沿用独立单份记录、CRC 和读回验证，任一保存失败返回错误及 `saved_mask`，已经成功写入的另一颗不回滚。成功只表示保存完成，**新值下次启动生效**，当前 INS 和原始采样不变；gyro-only 记录有效也不表示已完成加速度多姿态标定。

总执行期限 20 s（不包括等待 HostCmd 队列），失败不自动重试。HostCmd 在本次操作完成前无法执行其他耗时命令，HostComm、传感器和 INS 继续运行。已开始的操作断连后继续，旧会话响应丢弃；重连后用 `0x0A` 查询结果。重试同一活动请求不重复执行，但已完成响应只缓存 2 s，过期或新会话重发可启动新校准。

本地低优先级管理任务也可调用，不需要 HAL/RTOS 头：

```c
#include <SirinEmbedOS/Services/IMUCalibration.h>
SRNOS_IMUStaticResult calibrate_both(SRNOS_IMUStaticStatus *result)
{
    return SRNOS_IMUCalibrationRunStatic(SRNOS_IMU_CALIBRATION_BOTH_MASK, result);
}
/* 调用者提供状态对象；返回 SRNOS_IMU_STATIC_OK 表示所有请求记录已保存，
 * 下次启动应用。失败读取 result 的 failed_sensor/saved_mask/storage_status。 */
```

静态校准不使用独立线程、运行期内存分配或高优先级采样 hook。固定整数/浮点累计器在 HostCmd 中运行，仅进度副本使用短保护；等待时让出 CPU。

分区、v1 48 B / v2 60 B校准记录正文、启动解析和单份失败语义统一见 [EEPROM 设计](../../../Docs/Storage.md)。

### 转台生成的v2记录

v2在既有零偏/加速度对角修正之外加入`gyro_scale[3]`，范围0.8..1.2；INS在传感器轴上先减陀螺零偏再乘比例，随后做板/安装旋转。v1存量记录继续加载并使用比例1；保存v1仍输出48字节，v2为60字节。静态陀螺校准保留已存v2比例，不自动重估。SDK仅验证、持久化和应用系数；转台动作与拟合由应用自行实现。跨轴矩阵、温度曲线和绝对航向仍不包含。保存后下一次启动生效，不在承重状态自动复位。
