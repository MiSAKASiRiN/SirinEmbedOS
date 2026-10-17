# HostLink 服务注册、交互与遥测

CDC0 输出日志；CDC1 承载 HostLink V2，保留原 SRN-Link V2 的 COBS/CRC 线格式。COBS 只负责字节流定界，HostLink 负责路由、会话、命令与遥测；两者不等同于 EEPROM 格式。本文是当前协议入口，返回 [文档导航](README.md)。

- [COBS 帧与校验](#cobs-帧与校验)：外层字段、CRC、最大长度、解码顺序和报文样例。
- [服务与消息总表](#服务与消息总表)：SYSTEM/EXEC/CONFIG/CONTROL/TELEMETRY，以及状态码。
- [父项目服务接入](#父项目服务接入)：注册冻结、执行上下文、重试及背压。
- [参数 CONFIG 01–08](../Source/Services/Parameters/HostLink/README.md)：SCHEMA/READ/WRITE/SAVE/LOAD、状态、身份和稳定 ID。
- [专用 IMU 流](#专用-imu-流)、[INS 与环境流](#ins-与环境流版本1)、[SystemMonitor](#systemmonitor-v2)、[循环统计](#托管循环统计-0x0003)。
- [只读 SOURCE V2](#只读-source-v2)：父项目变量订阅，与专用二进制流分离。
- [手动静态 IMU 校准](#手动静态-imu-校准)：CONFIG 09/0A、保存后成功和部分失败。
- [EEPROM 设计](Storage.md)：落盘内容、默认值、加载/保存与单份记录风险。

## COBS 帧与校验

线帧为 `00 | COBS(raw) | 00`，raw 为 `12 B header | payload | 4 B CRC32`。COBS 编码区不含零字节，连续零分隔符忽略；USB 一次 read 可能只有半帧或包含多帧，必须跨 read 保留解码状态。坏 COBS、CRC、版本或长度的帧丢弃，超长帧丢弃至下一分隔符后重新定界，不把收包边界当作帧边界。

| raw 偏移 | 类型 | 内容 |
|---|---|---|
| 0 / 1 | u8 / u8 | magic=0xA5 / version=2 |
| 2 / 3 | u8 / u8 | kind：REQUEST=0、RESPONSE=1、EVENT=2；flags：MORE=bit0，其余必须 0 |
| 4 / 5 | u8 / u8 | service / message，二者共同决定路由 |
| 6 | u16 LE | payload 字节数，0–480 |
| 8 | u32 LE | sequence，请求与响应相同；事件序号由对应生产者维护 |
| 12 | bytes | payload；响应先放 `status:u16 LE`，后跟业务正文 |
| 12+payload_len | u32 LE | CRC32，覆盖前面的完整 header+payload |

CRC 为 CRC-32/ISO-HDLC：poly=0x04C11DB7（反射实现 0xEDB88320）、init=0xFFFFFFFF、refin/refout=true、xorout=0xFFFFFFFF；`123456789` 校验值=0xCBF43926，与 Python `zlib.crc32` 一致。不把分隔符、COBS 控制字节或 CRC 自身计入 CRC。先组 header/payload，再算 CRC 并小端附加，最后整体 COBS 编码。

最大 raw=496 B，COBS 区≤498 B，含两端分隔符线帧≤500 B。480 B 的响应 payload 已包含 2 B status，最大业务正文为 478 B；HostCmd 延后执行接口另外限制输入 256 B、输出正文 128 B，并非所有命令都可使用理论最大 payload。收到响应时必须同时匹配 service/message/sequence 并检查 status；ACCEPTED 只说明仍在处理，不能当作保存成功。

按现有 Python 编码器生成并回解的例子（均 sequence=1）：

```text
HELLO request，service=0 / message=1 / 空正文：
00 03 a5 02 01 01 02 01 01 02 01 01 01 05 77 d0 27 dd 00

双 IMU 静态校准 request，service=2 / message=9 / 正文 01 03：
00 03 a5 02 01 04 02 09 02 02 01 01 01 07 01 03 9e e7 6e da 00
```

第二个例子只用于协议实现核对；发送会执行约 5 s 校准并写 EEPROM。原始 `01 03` 不是完整线帧。COBS/CRC 不提供身份鉴权；HELLO 也仅为能力查询。

## 服务与消息总表

| service | message | 方向与用途 |
|---|---|---|
| 0x00 SYSTEM | 01 HELLO、02 QOS_GET、03 QOS_RESET、04 ECHO | REQUEST/RESPONSE，能力、计数、清计数、原样回显 |
| 0x01 EXEC | 01 CALL | REQUEST/延后 RESPONSE；请求 u16 command_id + 参数，命令由父项目启动注册 |
| 0x02 CONFIG | 01 SCHEMA、02 READ、03 WRITE、04 SAVE、05 LOAD | 业务参数；[逐字段协议](../Source/Services/Parameters/HostLink/README.md) |
| 0x02 CONFIG | 06 STATE、07 IDENTITY、08 STABLE_IDS | 参数状态、只读板卡身份、参数稳定 ID |
| 0x02 CONFIG | 09 STATIC_CALIBRATE、0A STATIC_STATUS | 手动静态校准与进度；[正文](#手动静态-imu-校准) |
| 0x03 CONTROL | 01 STATE | 主机→设备 EVENT，只更新父项目登记的控制邮箱，不写 EEPROM |
| 0x04 TELEMETRY | 01 SUBSCRIBE、02 SAMPLE | REQUEST/RESPONSE 订阅专用流；设备→主机 EVENT 携带流 ID 与批记录 |
| 0x04 TELEMETRY | 03/04/05/06/07 | SOURCE V1：LIST/SUBSCRIBE/CANCEL/CANCEL_ALL/DATA |
| 0x04 TELEMETRY | 08 IMU_STATUS、09 INS_STATUS | REQUEST/RESPONSE；与 CONFIG 09 不是同一个命令 |
| 0x04 TELEMETRY | 10/11/12/13/14（十六进制） | SOURCE V2：LIST/SUBSCRIBE/DATA/CANCEL/CANCEL_ALL |
| 0x7E TEST | 按测试注册 | 非默认业务能力，不能假定生产固件存在 |

专用 SAMPLE 流 ID 为 payload 内的 u16：0x0001 任务、0x0002 ADC/系统、0x0003 托管循环；0x0100 BMI、0x0101 LSM、0x0102 INS、0x0103 磁场、0x0104 气压。订阅依赖对应功能已启用。SOURCE 的 index、CONFIG 的参数 index/stable_id 都不是这些 stream_id。

CONTROL 正文为 `channel:u16, generation:u32, deadline_us:u32, data[]`。SDK 记录 received_ms 并保留最新邮箱副本；有 pending 时忽略不更新的 generation。父项目通过 TakeControl 消费并判断业务时效/安全条件，不能把 deadline 字段理解成 SDK 自动执行电机或自动停止控制器。EVENT 不按请求合同回复。

### 通用响应状态

| status | 含义 |
|---|---|
| 0x0000 / 0x0001 | OK / ACCEPTED（尚未完成） |
| 0x0101 / 0x0102 / 0x0103 | BAD_LENGTH / BAD_KIND / BAD_FLAGS |
| 0x0104 / 0x0105 | UNSUPPORTED_SERVICE / UNSUPPORTED_MESSAGE |
| 0x0106 / 0x0107 / 0x0108 | BUSY / CONFLICT / INVALID_ARGUMENT |
| 0x0109 / 0x010A / 0x010B | NOT_FOUND / TYPE_MISMATCH / OUT_OF_RANGE |
| 0x010C / 0x010D | STORAGE_ERROR / CALIBRATION_ERROR |
| 0x01FF | INTERNAL_ERROR |

0xFFFF DEFERRED_INTERNAL 是固件处理器内部结果，不应作为线上最终响应。外层 status、ParametersStatus、StorageStatus、IMUStaticResult 是不同枚举，按对应字段解释，不能混用数值。分页 MORE 表示主机需要用返回的 next 发起下一次请求，不是设备自动继续发送后半帧。

## 父项目服务接入

公共入口为 `SirinEmbedOS/Services/HostLink/HostLink.h`，不需要 FreeRTOS/CMSIS/HAL 头。`host_link_setup` 接收仅在启动回调期间有效的 `SRNOS_HostLinkRegistration *`：用 `SRNOS_HostLinkRegisterExec/RegisterTelemetryEx/RegisterControl` 声明业务能力，自定义消息/主动事件分别使用 `SRNOS_HostLinkRegisterHandler/RegisterTxTopic`。线程与 transport 由 SDK 管理，注册冻结后不能新增或改绑。完整示例、调用上下文、资源所有权及旧接口迁移见 [HostLink README](../Source/Services/HostLink/README.md)。

所有能力注册（命令、二进制流、标量源、控制邮箱、自定义消息和发送主题）均在启动装配后冻结，并检查重复 ID、重叠消息方向及内置路由冲突。订阅只改变已注册流的发送状态，不能重新注册或改变底层采样周期。EXEC、参数 SAVE/LOAD 和静态校准在 HostCmd 执行；目录/READ/WRITE、编码发送与轻量回调在 HostComm。

### 请求、完成与重放

活动请求表固定 10 项，在执行处理器之前保留槽位；它与完成后的 4 项重放缓存分开。活动身份为 `(service, message, sequence)`，同时记录请求指纹；同身份同内容的未完成重试返回 ACCEPTED，不重复执行；同身份不同内容返回 CONFLICT。活动表满则返回 BUSY，不执行新工作。未完成请求不按时间淘汰；同步请求在完成结果写入重放缓存后释放活动槽，延后请求保留至完成结果成功进入发送队列。会话重置会清理活动槽。

完成重放缓存保留至多 4 个结果，TTL 为 2 秒；满时可淘汰已完成记录，不影响活动请求。同步处理结果在发送尝试前保存，因此发送槽拥塞时同一请求的重试不会重新执行处理器。缓存容量与 TTL 是有界去重窗口，不能用作长期幂等保证。

HostCmd 工作队列与完成队列各 4 项。HostComm 从完成队列取出的结果由独立 **1 项 pending 槽**持有，直到发送队列接受或会话失效；拥塞时不重新塞回可能已被填满的完成队列。10 项活动容量对应工作队列 4 + 正在执行/等待提交完成的工作 1 + 完成队列 4 + pending 1。`SRNOS_HostLinkSubmitDeferred()` 仅允许当前 HostComm 请求处理器提交准确匹配的身份，每个请求最多接受一次；其他任务、错误身份及重复提交返回 BUSY。旧会话的工作完成不会响应到新会话。

FIFO 的完成/失败/取消通知暂存在原发送槽中，固定受 12 个 TX 槽约束，不另建无界队列。HostComm 在短临界区内取得回调并释放槽，随后在锁外调用，已接受的带回调记录最多通知一次；回调必须有界、非阻塞。回调只支持 FIFO，LATEST 不接受完成回调。

### 事件与截止时间调度

HostComm 等待 RX、TX 完成、工作完成和生产者提交通知，并按最近的遥测、标量或诊断快照截止时间唤醒。遥测 `poll_interval_ms` 为 0 时采用订阅周期向上取整到毫秒（周期也为 0 时取 1 ms）；内置 IMU/INS 有订阅时使用 1 ms，保留最老样本 4 ms 刷新语义；SystemMonitor 为 1 s。延迟唤醒只处理当前一轮，随后设定未来截止点，不追赶已错过的 poll。

没有订阅或待发送内容时，默认诊断快照截止时间为 100 ms，不维持 1 ms 常驻轮询。发送暂忙且无在途帧时按配置重试间隔等待，RX 每轮最多处理 1024 字节，预算耗尽后明确阻塞至少一个 tick，即使还有通知也不连续耗尽 CPU。这些限制不改变线协议、传感器 ODR、控制周期或监测采样频率。

### 固定存储预算

活动请求、完成重放与 pending 的固定存储不因 SOURCE V2 扩大：context 为 4800 B，活动记录 10 × 12 B 和重放 4 × 520 B 包含在其中；pending 为144 B，注册对象8 B。12项二进制遥测注册表仍为720 B，TX槽元数据每项44 B，回调不另建队列。SOURCE V2 的只读缓存为32 × 120 B加count/frozen共3843 B；HostComm的source目录引用为32 × 8 B、频率组为32 × 12 B。这些是固定存储容量；对象之间的链接对齐、最终 D1 用量和结构布局以当前构建 map 为准。

## 原始 IMU 观测配置

以下是仅观察原始 IMU、显式关闭业务存储命令的配置片段，不是 SDK 默认配置。在 `SRNOS_ApplicationConfigure()` 中设置：

```c
#include <SirinEmbedOS/Kernel/System.h>
#include <SirinEmbedOS/Services/HostLink/IMUTelemetry.h>

void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config)
{
    config->host_link_enabled = true;
    config->host_link_storage_enabled = false;
    config->load_parameters = false;
    config->host_link_setup = SRNOS_IMUTelemetryRegister;
    config->sensors.imu_observer = SRNOS_IMUTelemetryObserve;
    config->ins.enabled = false; // 仅原始采样；正式 INS 默认自动注册五条流
    config->sensors.lsm_enabled = true;
    config->sensors.heater_enabled = false;
    config->system_monitor_enabled = false; // 原始采样示例关闭后台 ADC/线程统计
    config->cycle_statistics_enabled = false; // 做开销基准时同时关闭循环 hook
}
```

包含 `SirinEmbedOS/Services/HostLink/IMUTelemetry.h`。观察回调与 HostLink 注册均在启动时冻结。回调运行在传感器发布线程，只复制到静态队列，不编码、不等待 USB。BMI Sensors API 始终发布原始测量。订阅不会改变硬件 ODR 或任务周期。

每颗 IMU 有 64 条样本缓存，满时丢弃新样本并增加 `overflow`；编码复用 HostComm 线程，每包最多六条，两条流轮流处理。正常无拥塞时，最老样本达到 4 ms 即尝试提交，给 1 ms 的 HostComm 唤醒预算留出余量；不是 USB 主机到达时间保证。离线时不重复发送上次快照。

`SRNOS_HostLinkRegisterTelemetryEx()` 支持固定订阅周期、校验、会话配置、发送完成和有界 poll 回调。`SRNOS_HostLinkRegisterTelemetry()` 简化入口采用 LATEST 策略。FIFO 成功表示复制入队，完成回调才表示底层发送完成；不代表主机应用已读取。12 个发送槽中 FIFO 最多使用八个，其余可用于命令及其他流量。FIFO 不合并、不覆盖，按入队序号排序。满时返回 `SRNOS_HOST_LINK_ERR_FULL`，生产者自行处理背压。

取消订阅清理该流未发送数据；DTR 下降、USB 断连清理订阅、请求缓存和会话数据。已交给 USB 的缓冲必须等真正完成或 endpoint 关闭确认取消后才回收。重新连接后主机工具先 HELLO 再重新订阅；HELLO 是能力查询，不是鉴权。旧会话的延后完成不会发送到新会话。SDK 默认启用启动参数加载、Storage 管理和 HostLink SAVE/LOAD/静态校准命令；上述原始观测片段显式关闭其中部分能力。身份工厂写入仍默认关闭。关闭 `load_parameters` 不等于关闭独立的 IMU 校准加载；完整开关与加载顺序见 [存储设计](Storage.md#启动和运行期)。

## 专用 IMU 流

沿用 `00 + COBS(header + payload + CRC32) + 00`。12 B 帧头为小端 `<6BHI>`：magic `0xA5`、version `2`、kind（request=0/response=1/event=2）、flags、service、message、payload 长度 u16、sequence u32。CRC32 覆盖未编码头及 payload，与 Python `zlib.crc32` 一致。响应 payload 前两个字节为状态 u16。最大 payload 480 B。

- HELLO：service `0` / message `1`，空请求。
- 订阅：service `4` / message `1`，payload `<HIB>` = stream u16、period_us u32、enabled u8。流 `0x0100` 为 BMI，`0x0101` 为 LSM；启用只接受 `1000` µs，其余返回 INVALID_ARGUMENT。取消使用 enabled=0。
- 样本事件：service `4` / message `2`。14 B 原遥测头 `<HIIHBB>` = stream、首条样本序号、首条 start_us 低 32 位、周期 1000、条数、记录大小 72；后接 1–6 条记录。周期为配置周期，精确采集区间以每条时间戳为准。
- 状态查询：service `4` / message `8`，空请求；成功响应去除状态 u16 后为 92 B。

每条记录固定 72 B，逐字段小端编码，不传 C 结构体：

| 偏移 | 类型 | 内容 |
|---|---|---|
| 0 | u8 | 格式版本 1 |
| 1 | u8 | `SRNOS_SensorState` |
| 2 | u16 | 保留，0 |
| 4 | u32 | 样本序号 |
| 8 | u32 | `valid_fields`，见 Sensors.h |
| 12、20 | u64 各一 | 采集起止时间，DWT 单调微秒，主控采集时间 |
| 28 | i16 × 3 | 原始加速度 XYZ |
| 34 | i16 × 3 | 原始角速度 XYZ |
| 40 | i16 | 原始温度 |
| 42 | u16 | 保留，0 |
| 44 | float32 × 3 | 加速度 XYZ，m/s² |
| 56 | float32 × 3 | 角速度 XYZ，rad/s |
| 68 | float32 | 温度，°C |

状态正文为版本 u8=1、设备数 u8=2、保留 u16=0，然后两条 44 B `<HBBIQHH6I>`：stream、state、enabled、最新序号、last_sample_us、缓存深度、历史最高深度，以及 enqueued/overflow/sent/failed/discarded/samples 六个 u32。计数从启动累计；取消/重连清理的样本记入 discarded，不清零计数。`sent` 是 USB 完成的样本数，`samples` 是驱动发布数，未订阅时也会增长。健康快照与通信计数各自一致，不是跨任务原子采样。计数做差需处理 u32 环绕。

## Python CLI

需要保存完整逐样本数据时使用 `Tools/HostLink/imu_capture.py`，见 [采集说明](../Tools/HostLink/README.md)。它输出双 CSV、原始接收字节、每秒状态与元数据，支持预热/定时停止，断连后停止而不拼接会话。下述 `imu_telemetry.py` 仍为只显示的监视器。

在 SDK 根目录：

```sh
python3 -m venv build/hostlink-venv
build/hostlink-venv/bin/pip install -r Tools/HostLink/requirements.txt
build/hostlink-venv/bin/python Tools/HostLink/imu_telemetry.py \
  --port CDC1_PATH --duration 600
```

将 `CDC1_PATH` 替换为本机实际 **CDC1** 路径；先用 `build/hostlink-venv/bin/python -m serial.tools.list_ports -v` 列出端口，路径随枚举变化。CLI 拒绝占用中的端口；CDC 波特率设置不改变 USB 链路速率。每秒显示两颗 IMU 完整最新值、实际接收率、序号缺口、重复与设备计数，每十秒输出采集率/接收率及协议错误。每个正常十秒窗口目标均 ≥990 次/秒；不以配置周期代替测量。`--duration 0` 持续运行，Ctrl-C 取消订阅。重连重新握手订阅；无需打开 CDC0。

状态/速率输出到 stdout，不录制文件。对进程暂停读取的测试可能被主机 USB/TTY 缓冲吸收，未触发 overflow 时不能宣称验证了设备满队列行为。不同固件的采集时长与实测范围见 [Validation.md](Validation.md)，不能把某次长测外推到其他配置或固件。

C 与 Python 共用 `Tests/Services/IMUTelemetry/golden_record.hex`；主机套件覆盖字段字节序、溢出、会话清理和坏帧恢复。实板短测不代替满负载 CAN、传感器校准或实际声光观察。

### 温度与终端显示

`imu_capture.py` 与 `imu_telemetry.py` 默认使用易读状态行，显示两颗 IMU 的实测温度、接收率、样本数、缺序和设备累计溢出/发送失败；离线温度标为旧值。`--json` 保留机器可读终端输出。CSV、原始二进制和状态文件的字段见采集工具说明。SensorBringUp 当前优先控制 LSM 为60°C、LSM失效时回退BMI，CDC0显示实际来源和占空比；先用 monitor 观察温度趋稳，再开始正式静置采集，`--warmup` 只是时间分段，不会自动判定温稳。

## INS 与环境流（版本1）

正式 INS 开启时自动注册。注册与 HostLink 会话清理方式沿用上述合同：只有订阅后的新数据入队，满时丢新并计数；取消、DTR下降或断连清理未发数据，USB引用中的缓冲仍等完成/确认取消。

| 流 | ID | 订阅周期 µs | 单记录 | 缓存 | 每包上限 |
|---|---|---:|---:|---:|---:|
| 姿态 | `0x0102` | 1000 | 108 B | 64条 | 4条 |
| 磁力计 | `0x0103` | 10000 | 52 B | 16条 | 8条 |
| 气压计 | `0x0104` | 62500 | 44 B | 8条 | 8条 |

14 B批头保持不变，新三条流的时间字段为首条**姿态时间/采集结束时间**低32位；原双 IMU 仍为采集开始时间。周期是订阅合同，环境流只在真实新样本时发送，不补齐到指定速率。姿态未收敛或失效时不发送假有效姿态；使用状态查询区分等待、离线和通信问题。队列到4ms尝试发送，传输仍受USB拥塞影响。

所有整数及 IEEE754 float32 显式小端编码。Python标准库格式与固件共享黄金向量：`Tests/Services/IMUTelemetry/golden_{ins,mag,pressure}.hex`。

姿态108 B，Python `<4B3I2Q4I15f>`：

| 偏移 | 字段 |
|---|---|
| 0..3 | u8版本1、phase、source_mask、new_sample_mask |
| 4/8/12 | u32 sequence、epoch、INS flags |
| 16/24 | u64 attitude_us、published_us |
| 32/40 | u32[2]输入序号、u32[2]年龄µs |
| 48 | float[2]融合权重 |
| 56 | float[4]四元数wxyz，机器人到世界 |
| 72 | float[3]roll/pitch/yaw，rad |
| 84 | float[3]机器人系角速度，rad/s |
| 96 | float[3]机器人系比力，m/s²，包含重力 |

磁场52 B，Python `<BBHIIQQ4h4f>`：版本u8、state u8、保留u16=0、序号u32、有效位u32、起止u64各一（共28 B），随后器件轴整数磁场XYZ i16与温度i16，机器人系磁场XYZ float和温度float。

气压44 B，Python `<BBHIIQQ2i2f>`：相同28 B头，随后原始压力/温度i32各一、补偿压力Pa/温度°C float各一。磁场和气压的原始整数没有安装旋转；矢量SI数据已使用正式INS坐标合同。

### INS状态查询

service4/message9，空请求；去掉响应状态u16后为256 B。可在温稳/观测/收敛期间调用。

| 偏移 | 内容 |
|---|---|
| 0..3 | u8版本1、phase、健康六轴mask、heater flags（启用1/温稳2/过温4/硬故障8/反馈缺失16） |
| 4/8/12 | flags u32、epoch u32、updated_us u64 |
| 20 | 11个u32：cycles、publications、dual、single、predicted、missing、recoveries、missed_deadlines、max_execution_us、max_period_us、max_copy_us |
| 64/65 | i8反馈来源（−1无/0BMI/1LSM）、u8过温来源mask |
| 68 | 四颗传感器state u8[4] |
| 72 | float目标°C、反馈°C、PWM比例 |
| 84 | u32过温次数、恢复次数、来源选择次数 |
| 96 | float[4]冻结安装四元数 |
| 112/160/208 | 三条流各48 B：stream u16、enabled u8、保留u8、9个u32（最新序号、深度、高水位、入队、溢出、完成、失败、主动清理、容量）、剩余8 B保留0 |

位与状态含义见 `Services/INS.h` 和 [INS合同](INS.md)。计数为启动累计u32，可回绕；测量窗口取无符号差。发送完成只证明USB完成，不证明主机用户程序消费。原始流状态查询4/8及CSV列完全保留。

```sh
build/hostlink-venv/bin/python Tools/HostLink/imu_telemetry.py \
  --port CDC1_PATH --streams bmi lsm ins mag pressure --duration 600 --json
```

默认省略`--streams`仍仅两路原始流。终端每秒显示姿态、温度、来源/权重、温稳/保护状态和速率；`--json`附完整状态及每10秒实际发布/接收速率。重连重新握手和订阅，退出取消。`imu_capture.py` 默认保存双 IMU，也支持 `--streams bmi lsm ins mag pressure` 分流保存五路 CSV；未选择的流不创建文件。现有工具不发送参数 SAVE/LOAD 或静态校准命令。

## SystemMonitor V2

正式默认固件注册 0x0001（任务）、0x0002（ADC/系统）及 0x0003（托管循环）三条流；开启 `system_monitor_enabled` 的构建才注册。订阅周期必须为 1,000,000 µs，采样周期不随订阅改变。与原未接通的 HSKP V1 不兼容；版本字节为 2。沿用 HostLink V2 的 `TELEMETRY/SAMPLE`（service=4、message=2）和 14 B `<HIIHBB` 外层头。sample_period_us=0，表示使用记录内显式时间戳（uint16 无法表示 1 s）；count 分别为 1 或 1..4，size 为 128 或 112，device_time_us 为系统快照 updated_us 的低 32 位。全部数值小端，float 为 IEEE754 binary32。

ADC 记录：0=version(u8)，1=ADC state(u8)，2..3=0；4=snapshot sequence(u32)，8=system flags(u32)，12=ADC sequence(u32)；16/24/32=updated/start/completed(u64)；40/44=valid/quality(u32)；48=五个 raw(u16)，58..59=0；60=六个 float（A、输入V、VBAT V、VREFINT V、温度°C、VDDA V）；84=11个 u32（errors、timeouts、recoveries、busy_ppm、idle_ppm、monitor_cpu_ppm、task_count、max_critical_us、max_wake_late_us、skipped_cycles、slot_overflows）。

任务记录：0=version，1=slot，2=flags，3=state（均 u8，state 沿用 FreeRTOS Running=0/Ready=1/Blocked=2/Suspended=3/Deleted=4）；4=generation、8=snapshot sequence（u32）；12/13=current/base priority(u8，FreeRTOS 原始优先级)；14=stack_free_words(u16，65535=未测量)；16/24/32=sampled_us/window_us/runtime_us(u64)；40/44/48/52/56=cpu_ppm/switch_count/ready_count/max_run_us/max_ready_us(u32)；60=16字节零结尾名称；76=系统updated_us(u64)；84=7个u32（system flags、busy、idle、monitor CPU、task count、max critical、slot overflows）。无活跃任务时允许 slot=0 的空记录以传系统汇总。任务 identity 为(slot,generation)，各任务窗口独立，不应把四条记录相加当作同步CPU总和。

flags/valid/quality 位定义以 SystemMonitor.h 为准。CPU ppm 除以 10,000 得百分比，时间为 µs；每次任务只发最近采样窗口，不汇总未上报的全部轮转间隔。系统总CPU通过 Idle 推算。HostComm 的 Debug Watch 栈字段为 UINT32_MAX，表示未测量，不进行周期栈扫描。

每流每秒最多一次尝试；采用 LATEST 合并旧的待发送记录，无独立队列和重试自旋。会话断开后取消订阅，由 HostLink 清理待发送内容；已经由 USB 引用的槽依照现有传输生命周期释放。业务命令、INS FIFO 的 QoS 保持。

```sh
build/hostlink-venv/bin/python Tools/HostLink/system_monitor.py \
  --port CDC1_PATH --duration 60 --output captures/system-monitor-01
```

输出目录必须不存在，保存 records.jsonl 和 metadata.json；`--json` 输出机器可读终端记录。每行有主机收到时刻与设备时间，不能用主机到达时间代表 ADC 采集时刻。显示逐字段有效性、质量标记和设备数据年龄；基础任务流显示片段/Ready延迟，循环耗时与周期偏差单独来自 0x0003。正常结束、Ctrl-C、异常均写元数据；中断/断线不标 complete、不自动连接后拼接不同固件会话。计数与解码错误供核查，不把完整保存等同于ADC物理量有效或所有任务已轮转。

## 托管循环统计 0x0003

遥测注册上限 12，继续启动期冻结和 ID 冲突校验。0x0001/0x0002 仍使用上述 V2 记录；0x0003 记录版本为 1，每秒最多四条，每条 112 B，LATEST 策略。使用相同 service=4/message=2/14 B 外层头，sample_period_us=0；device_time_us 是本次系统 updated_us 低 32 位。连续订阅、正常链路下 64 槽最多 16 秒轮转；采样仍每秒独立执行，拥塞或断连不积压、不影响采样。

| 偏移 | 类型 | 字段 |
|---|---|---|
| 0/1/2/3 | u8 各一 | 版本 1、槽号、flags、保留 0 |
| 4 | u32 | 生命周期 generation，与槽号共同识别任务 |
| 8 | char[16] | 零结尾名称 |
| 24/32 | u64 | sampled_us / window_us，该任务自己的实际统计窗口 |
| 40/44/48 | u32 | period_ms / completed / jitter_samples |
| 52/56/60 | u32 | execution_min/mean/max_us |
| 64/68 | u32 | start_late_mean/max_us |
| 72/76 | i32 | jitter_min/max_us，开始间隔减目标周期 |
| 80 | u32 | jitter_abs_mean_us |
| 84/88 | u32 | deadline_misses / skipped |
| 92/96 | u32 | hook_mean/max_ns，诊断估计值 |
| 100 | u64 | 系统 updated_us，用于年龄对照 |
| 108 | u32 | 保留 0 |

flags：ACTIVE=1、PERIODIC=2、MEASURED=4、JITTER_VALID=8、STALE=16、SATURATED=32。无活跃任务允许 slot=0 空记录。只有 PERIODIC 才解释启动延迟/抖动/截止字段，只有 MEASURED/JITTER_VALID 才有对应窗口数据；事件/相对等待没有虚构周期。CPU 表示调度运行占用，execution 为 step 墙钟，包含抢占/同步 I/O/ISR，排除框架普通等待、初始化、清理。以完成轮次归入统计窗口，不能把墙钟总和当成该窗口 CPU。

hook 成本估计不含末尾记账/返回，可能包含抢占，只用于观察趋势；1 µs 无抢占基准、CPU 增量和采样临界区目标必须另做板测。旧固件缺少 0x0003 返回 NOT_FOUND 时 CLI 保留两条基础流；JSONL/metadata 写入支持的流和有效性，不将未支持误判为零抖动。

## 只读 SOURCE V1

TELEMETRY service=4 的 `0x03 LIST / 0x04 SUBSCRIBE / 0x05 CANCEL / 0x06 CANCEL_ALL / 0x07 DATA` 保留原编码。它与下述 V2 共用冻结目录和每源唯一订阅。标量类型：1=float32、2=int32、3=uint32、4=bool（单字节 0/1）；这些是独立于参数目录的索引。

- LIST 请求 `cursor:u16, limit:u8`；正文头为 `catalogHash:u32, total:u16, next:u16, count:u8`。每项 `index:u16, type:u8, valueSize:u8, currentHz:u8, nameLen:u8, unitLen:u8, name, unit`，字符串无 NUL。next=65535 表示结束，否则设置 MORE。V2 已订阅源的 V1 currentHz 显示 0。
- SUBSCRIBE 请求 `catalogHash:u32, count:u8, {index:u16, rateHz:u8}[]`；CANCEL 为同一头后接 `index:u16[]`；CANCEL_ALL 只有 hash。成功正文统一 `catalogHash:u32, activeCount:u8, totalRateHz:u16`，统计两版本合计。
- DATA 为 EVENT，12 B 头：`catalogHash:u32, encodeTimeMs:u32, count:u8, reserved[3]=0`；每项 `index:u16, type:u8, valueSize:u8, value[]`。无有效样本时 size=0 且无 value；V1 不带每项采样时刻或序号，不应被当成同步采样。

V1 为 5–100 Hz、步进 5，V1 合计≤500 标量/s；与 V2 合计≤8000 标量/s。目录变更先重新 LIST，不能用旧目录下的 index 直接写入新会话订阅。

## 只读 SOURCE V2

只读变量来自启动期冻结的 `SRNOS_TelemetryRegister()` / `SRNOS::Telemetry::Value<T>` 目录，与可远程写入的业务 Parameters 完全分开。支持 type=1 float32、2 int32、3 uint32、4 bool；前3种值为4 B，bool为1 B。目录最多32项，ID是冻结目录槽号，名称唯一。业务主动发布，HostComm只取缓存副本；未发布值 valid=0、sequence=0。采样时间由业务传入，编码时间由HostComm生成，两者不可混用。实际频率和记录完整性受主机读取及USB拥塞影响。

为避免与现有 IMU/INS 状态请求 `0x08/0x09` 冲突，SOURCE V2 固定使用 **`0x10..0x14`**，不使用早期计划的 `0x08..0x0C`，均属于 TELEMETRY service=4；旧状态查询和二进制流保持。以下所有整数显式小端，不传C结构体。

| 消息 | ID | 请求/事件正文 |
|---|---|---|
| LIST V2 | `0x10` | 请求cursor u16、limit u8；分页返回目录 |
| SUBSCRIBE V2 | `0x11` | catalogHash u32、count u8，随后count项(index u16、rateHz u16) |
| DATA V2 | `0x12` | 设备事件；头与记录见下文 |
| CANCEL V2 | `0x13` | catalogHash u32、count u8，随后count个index u16 |
| CANCEL_ALL V2 | `0x14` | catalogHash u32，仅取消V2订阅 |

LIST响应去除通用status u16后，头为catalogHash u32、total u16、nextCursor u16、count u8，共9 B；nextCursor=65535表示结束，否则设置MORE。每项为index u16、type u8、valueSize u8、maxHz u16、currentHz u16、subscriptionVersion u8（0/1/2）、nameLen u8、unitLen u8，随后原始名称和单位字节，不带NUL。固定部分11 B，整包按响应容量分页。SUBSCRIBE/CANCEL成功响应去除status后仍为catalogHash u32、activeCount u8、totalRateHz u16，共7 B，统计当前两版本合计订阅。

订阅频率5～1000 Hz且为5的倍数，不得超过变量maxHz。V1仍用原03 LIST/04 SUBSCRIBE/05 CANCEL/06 CANCEL_ALL/07 DATA及u8频率，限制5～100 Hz和V1合计500标量每秒；两版本合计上限8000标量每秒。每源只保留一份订阅，V1与V2后订阅替换前订阅。重复ID、超范围、目录hash不符或总额超限时整批拒绝，不部分更新；取消只能操作对应版本，取消另一版本返回NOT_FOUND。断连清理两版本订阅；重新连接不自动恢复。目录hash仍基于冻结名称/单位/类型，与V1共用。

DATA V2头16 B：catalogHash u32（0）、encodeTimeUs u64（4）、count u8（12）、reserved[3]=0（13）。随后最多16项：index u16（0）、type u8（2）、valid u8（3，0或1）、sequence u32（4）、sampleTimeUs u64（8）、value（16，按type为4或1 B）。单项最大20 B，包最大336 B。无效值仍携带发布时的值、序号、时间；valid为0时不得当作正常测量。序号每次Publish递增，可回绕；LATEST覆盖、订阅限频和断连会造成序号间隔，这是预期的最新值策略，不代表无损记录。

同频率且同版本的源加入固定组槽。包按目录索引0～15/16～31两半分组，key由组槽和半区产生；同组旧报告可被新报告替换，不让一个频率组覆盖另一频率组。最多扫描32组，未建立200项空频率表。延迟调度只发当前最新轮次并记录错过次数，不集中补发历史值。Publish不唤醒HostComm，不受订阅、断连或TX拥塞影响；目录缓存每项单独短临界区复制，字段编码在锁外。

主机测试覆盖C/C++登记、类型/容量/冻结、显式时间/有效性/序号、V1旧格式、V2分页/限额与原子拒绝、版本替换、16项分包、多频率背压合并、调度时钟回绕及断连。此次不提供上位机实现，也未烧录；高频吞吐与对INS/CAN影响须另做板端验收。

## 手动静态 IMU 校准

CONFIG service=`0x02`，REQUEST `0x09`（执行）和 `0x0A`（读取最近任务状态），不改变既有 CONFIG/TELEMETRY 编码。公共常量位于 `Services/HostLink/IMUCalibrationProtocol.h`。注册在启动期冻结；与自定义路由冲突时启动失败，不能覆盖。默认固定路由使用 22/24 槽，父项目仍有 2 槽（实际可用量随其注册变化）。

`0x09` 正文严格为 `[u8 version=1, u8 sensor_mask]`：mask 1=BMI088，2=LSM6DS3，3=两颗。例：`01 03`。`host_link_storage_enabled=false` 时拒绝执行；只读查询仍可用。命令在低优先级 HostCmd 中观察约 5 s、校验后保存 EEPROM，完成才返回最终响应。最长执行期限 20 s，不含队列等待及发送拥塞；主机应留出这些余量。状态查询不进入耗时队列，可以在校准中发出。

`0x0A` 正文为空；返回最近一次开始执行的任务，不表示尚在 HostCmd 队列中的任务。phase=IDLE 表示本次启动尚未执行。运行中可观察进度，每约 100 ms 更新一次。上位机应把任务 job_id 与当前请求/会话关联，不把旧任务 DONE 当成刚排队命令完成。

两种响应都使用既有 `u16 status` 前缀，以下偏移从其后的 **68 B 正文** 开始，整数及 float32 均小端。状态查询本身成功时 status=OK，即使正文记录最近校准失败。拒绝执行的即时错误可不带正文。

| 正文偏移 | 类型 | 含义 |
|---|---|---|
| 0 | u8 | 正文版本 1 |
| 1 | u8 | phase：0 IDLE、1 PREPARING、2 SAMPLING、3 SAVING、4 DONE、5 FAILED |
| 2 / 3 | u8 / u8 | requested_mask / saved_mask |
| 4 / 5 | u8 / u8 | failed_sensor：0 BMI、1 LSM、255 无单一器件；reserved=0 |
| 6 | u16 | 校准 result，见下表 |
| 8 / 12 | u32 / u32 | job_id / elapsed_ms，不含排队时间 |
| 16 / 20 | u32 / u32 | BMI / LSM 实际观察样本数 |
| 24 / 26 | u16 / u16 | BMI / LSM 最近存储结果（SRNOS_StorageStatus）；未选中或尚未存储时不可作保存成功依据 |
| 28 / 32 | u32 / u32 | BMI / LSM 本次成功保存 revision；以 saved_mask 为准 |
| 36 / 48 | 3×float32 | BMI / LSM SENSOR 轴 gyro bias，rad/s，完成采集校验后有效 |
| 60 / 64 | float32 | BMI / LSM 平均校准温度，°C，完成采集校验后有效 |

| result | 含义 |
|---|---|
| 0 | OK：所有选中的记录已完成校验保存 |
| 1 / 2 / 3 | BUSY / INVALID / CONTEXT |
| 4 / 5 / 6 | NOT_READY / SENSOR_ERROR / STALE |
| 7 / 8 / 9 / 10 | MOVING / TEMPERATURE / TIMEOUT / STORAGE_ERROR |

最终外层 status：成功 `0x0000`；忙 `0x0106`；非法参数 `0x0108`；存储失败 `0x010C`；静止、温度、采样或期限异常使用 `0x010D CALIBRATION_ERROR`，细分原因取正文 result。存储结果枚举为 0 OK、1 BUSY、2 DISABLED、3 INVALID_ARGUMENT、4 CONTEXT_ERROR、5 NOT_FOUND、6 CORRUPT、7 INCOMPATIBLE、8 IO_ERROR、9 TIMEOUT、10 VERIFY_ERROR、11 READ_ONLY。

需要任意姿态下真实静止，只估计陀螺零偏，保留已有有效加速度校正；无有效记录时为零偏 0、比例 1。两颗都通过观察后才开始写入，但两个单份记录不具备组合原子性：部分保存返回错误和掩码，不能只看某一个 storage_status=0 宣称成功。新值下次启动应用；采集异常不写 EEPROM，保存中异常可能使正在更新的单份记录失效。

相同活动请求的重试继续由活动表去重（不会因超过 2 s 而重复执行），相同序号不同正文冲突。完成重放缓存仍为 4 项/2 s，不能作为永久幂等保证。新的独立请求可能排队，执行中的校准被状态检查识别后新请求返回 BUSY。会话断开不能回滚已执行操作；未开始的旧会话工作丢弃，已开始的工作继续保存，旧响应不进入新会话。重连先查询状态，避免盲目再次写入。
