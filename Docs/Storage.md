# EEPROM 存储设计与业务使用

截至 2026-09-29，以当前自维护实现为准。FT24C128A 器件读写已有板测；这里的单份记录、业务参数持久化及新静态校准已通过主机/构建检查，尚未完成对应真实断电和组合负载验收。历史结果不自动覆盖新固件。返回 [文档导航](README.md)。

## 数据与职责

| 数据 | 父项目入口 | 可由主机修改 | EEPROM / 生效时机 |
|---|---|---|---|
| PID 等业务参数 | `Parameters/Parameters.h/.hpp`，`Persistent<T>` | CONFIG WRITE；必须有默认值、范围和稳定 ID | SAVE 显式保存；RAM 修改由应用在周期边界读取快照应用 |
| 编码器等观测值 | `Telemetry.h/.hpp`，`Value<T>::Publish` | 只允许订阅，不允许 WRITE | 不存 EEPROM；只保留最新样本 |
| 两颗 IMU 校准 | `IMUCalibration.h` | CONFIG 0x09 触发静止陀螺零偏校准 | 校验后自动保存，各一份；下次启动生效 |
| PCB 序列号 | `Storage.h` | CONFIG 0x07 只读；无远程写入口 | 严格 20 位 ASCII 数字，保留前导零；仅本地工厂权限可写 |
| CAN/UART 路由、服务开关、线程资源 | `Kernel/System.h` 的启动配置 | 不允许放入业务参数目录 | 启动冻结，不属于 EEPROM 业务数据 |

Storage 拥有地址、格式、CRC、提交页、互斥与状态；Parameters/INS 拥有数据合法性、默认值和应用方式；FT24C128A 驱动仅负责分页、I2C、写周期和逐段校验。父项目不包含私有存储头或 HAL/RTOS 头。

## 分区与格式

| 地址 | 用途 | 最大正文 |
|---|---|---:|
| 0x0000–0x03FF | 板卡身份 | 896 B（v1 实际为 20 B 序列号） |
| 0x0400–0x07FF | BMI088 校准 | 896 B |
| 0x0800–0x0BFF | LSM6DS3 校准 | 896 B |
| 0x0C00–0x0FFF | 校准扩展预留 | 禁止原始写 |
| 0x1000–0x2FFF | 应用业务参数 | 8064 B |
| 0x3000–0x3FFF | 后续扩展 | 原始接口可用 |

每种记录仅一份，正文从分区起点后 64 B 开始，提交页固定在分区最后 64 B。头/提交页所有整数采用小端序，保留字节写零。内容不直接转储 C 结构体。

| 记录头偏移 | 长度 | 含义 |
|---|---:|---|
| 0 | 8 | `SRNOSNV1` |
| 8 / 10 | 2 / 2 | 格式版本 1 / 类别 1..4 |
| 12 / 16 / 20 | 4 / 4 / 4 | 正文长度 / revision / owner_id |
| 24 / 26 | 2 / 2 | 数据 schema / 头长度 64 |
| 28 | 32 | 保留零 |
| 60 | 4 | CRC-32/ISO-HDLC（头前 60 B + 正文） |

提交页为 `SRNOSOK1`（8 B），随后是类别/格式版本（各 2 B）、正文长度/revision/记录 CRC（各 4 B），24..59 为零，最后 4 B 为提交页前 60 B 的 CRC。revision 可自然回绕；单份记录不靠 revision 挑选副本。空白头（全 FF 或 00）为 NOT_FOUND；损坏、格式/归属不匹配和 I/O 错误分别报告，不自动擦除、初始化或写默认值。

保存顺序为：验证当前记录（相同内容跳过写入）→将提交页清零并读回确认→写头和正文→完整读回 CRC/内容检查→写提交页→再次检查完整记录。CRC 提取到独立 Core 增量接口，HostLink 继续使用相同 CRC。无 A/B 或备份，保存中断可能令本记录失效；CRC 不是掉电原子写入保证。其他分区不参与本次写入。最终读回失败或超时可能发生在提交页已经写好之后；返回失败不表示介质未变化，也不执行回滚，下一次加载仍须完整校验。

## 三种正文

### 板卡身份

类别=1、owner_id=0、schema=1。正文恰好 20 B，每字节必须为 ASCII `0`～`9`，不含 NUL。公共快照另加结尾 NUL；失效时标记未配置并返回空字符串，不能把二十个 ASCII 零当作未配置。主机身份响应的无效内容是 20 个零字节。

### IMU 校准

BMI088 类别=2，LSM6DS3 类别=3；owner_id=`0x53494D55`，schema=1。正文恰好 48 B，全部小端，浮点为 IEEE754 float32：

| 偏移 | 类型 | 内容 |
|---|---|---|
| 0 / 2 / 4 / 6 | u16 各一 | sensor：0 BMI / 1 LSM；model_version=1；axes_version=1；保留 0 |
| 8 | float32[3] | SENSOR 轴 gyro bias，rad/s |
| 20 | float32[3] | SENSOR 轴 accel bias，m/s² |
| 32 | float32[3] | accel 对角比例，无量纲 |
| 44 | float32 | 校准温度，°C |

`gyro = measured - bias`；`accel = (measured - bias) × scale`，在板级/安装旋转前应用。原始 Sensors 快照不变。范围与降级见 [INS](../Source/Services/INS/README.md#校准持久化)。静止校准只生成陀螺 bias/温度，保留有效的 accel 系数；新值保存后不立即改变当前融合，两个记录不具备组合原子性。触发/结果见 [CONFIG 校准协议](HostLink.md#手动静态-imu-校准)。

### 业务参数

类别=4，owner_id=父项目非零 `parameter_application_id`，schema=非零 `parameter_compatibility_version`。正文 8 B 头：u32 magic=`0x31505253`、u16 格式=1、u16 条数；随后每项 12 B，稳定 ID 严格递增：

| 项内偏移 | 类型 | 内容 |
|---|---|---|
| 0 | u32 | 非零 stable_id |
| 4 / 5 / 6 | u8 / u8 / u16 | type / value_size / 保留 0 |
| 8 | 4 B | 值；type 1=float32、2=int32、3=uint32、4=bool |

前三种 value_size=4；bool size=1，值为 0/1，尾部三字节为零。默认 32 项，保存快照为 `8+12×32=392 B`；记录容量 8064 B 不等于默认允许注册 672 项，注册容量另受配置限制。名称、默认值和范围留在固件声明，EEPROM 按 stable_id、类型及当前范围匹配，未知项忽略，新增/不兼容/越界项回默认。

`layout_hash` 是当前主机目录的版本标记，变更注册顺序、默认值等可使其变化；它不充当 EEPROM 归属。只有 application_id/兼容版本、存储格式及逐项 ID/类型/范围决定保存值能否加载。参数与遥测目录也不共用索引或 hash。

## 启动和运行期

1. 启动冻结配置与变量注册。默认 `storage.enabled=true`、`load_parameters=true`、`host_link_storage_enabled=true`，工厂身份写入默认关闭。
2. AppInit 在共同 2 s 截止时间内依次解析身份、双 IMU 校准和业务参数；截止时间是总预算，不是每条记录各 2 s。INS 等待校准解析时继续温控和状态更新，超时不接纳迟到校准。
3. 参数加载尝试结束后才放行父应用；无效校准被忽略，参与融合的任一 IMU 无有效校准则明确 UNCALIBRATED/DEGRADED，不把存储错误视作传感器离线。
4. CONFIG WRITE 只改 RAM；SAVE 在 HostCmd 真正开始执行时冻结快照，成功更新 saved_generation；期间 WRITE 产生的新 generation 仍为 dirty。LOAD 遇到同时 WRITE 则返回冲突。

| 情况 | 结果 |
|---|---|
| 启动参数空白/损坏/归属不匹配 | 使用声明默认值，报告回退，不自动写回 |
| 运行期 LOAD 失败 | 保留当前 RAM，返回错误 |
| 任一记录写入中断 | 该记录可能失效；无旧副本可恢复，其他分区不随之回滚 |
| 两颗校准仅一颗保存成功 | 整体失败，saved_mask 明确部分结果；当前 active 不变 |
| 同时发起存储 | 全局单操作租约，竞争立即 BUSY；不新增存储线程 |
| 内容与有效记录相同 | 跳过写入，不自动逐次保存 RAM 修改 |
| HostLink 禁用 | 本地 Storage/Parameters/校准能力仍可使用 |

Storage 默认读 2 s、写 5 s，接口的 timeout=0 表示使用默认总期限；不要套用 Kernel 的“0=非阻塞”。原始 EEPROM 的 timeout 是每段提交/传输期限，两者不同。同步接口只在 IRQ 开启、调度运行的任务上下文调用，不能放入控制周期或 HostComm 轻量回调。静态校准总执行期限 20 s，其中约 5 s 为观察窗口；队列等待不计入执行期限。

## 父项目及主机接入

- 变量声明、默认值与一致快照：[Parameters](../Source/Services/Parameters/README.md)、[示例](../Examples/Parameters/README.md)。
- 编码器等只读发布：[Telemetry](../Source/Services/Telemetry/README.md)。单源上限 1 kHz、总预算 8000 标量/s，不承诺任意负载下无丢样。
- 参数目录/READ/WRITE/SAVE/LOAD/STATE/IDENTITY：[CONFIG 01–08](../Source/Services/Parameters/HostLink/README.md)。
- COBS、CRC、会话/重试及 CONFIG 09/0A：[HostLink](HostLink.md)。
- 20 位身份工厂配置：[Storage API](../Source/Services/Storage/README.md#工厂身份配置示例)。原始全片维护须启动时关闭 Storage 管理；不允许运行时绕过保护区。

## 证据与待测

[持久化主机/构建记录](Validation/Persistence-20260928/README.md)覆盖格式、逐字节中断模型、并发及参数升级；[静态校准记录](Validation/StaticIMU-20260929/README.md)覆盖算法、异常和协议。真实断电、校准精度、保存与 INS/I2C/CAN 共存、CDC 断连和栈余量仍须对应固件板测。源码不自动迁移旧 24C02 格式，不提供 A/B、日志轮换、用户标定存储备份或任意 EEPROM 地址的主机写入口。
