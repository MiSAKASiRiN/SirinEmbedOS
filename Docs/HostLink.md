# HostLink 传感器与 INS 遥测

CDC0 输出日志；CDC1 使用原有 SRN-Link V2（二进制 COBS + CRC32）。SensorBringUp 在收到订阅后发送 BMI088/LSM 的新样本；未订阅不缓存。两颗 IMU 采用各自坐标系、各自采集时间，不能据包顺序假定同步。正式 INS 默认额外注册姿态、磁场与气压流，见文末扩展。

## 固件接入

在 `SRNOS_ApplicationConfigure()` 中设置：

```c
config->host_link_enabled = true;
config->host_link_storage_enabled = false;
config->load_parameters = false;
config->host_link_setup = SRNOS_IMUTelemetryRegister;
config->sensors.imu_observer = SRNOS_IMUTelemetryObserve;
config->ins.enabled = false; // 仅原始采样；正式 INS 默认自动注册五条流
config->sensors.lsm_enabled = true;
config->sensors.heater_enabled = false;
config->diagnostics_enabled = false;
```

包含 `SirinEmbedOS/Services/HostLink/IMUTelemetry.h`。观察回调与 HostLink 注册均在启动时冻结。回调运行在传感器发布线程，只复制到静态队列，不编码、不等待 USB。BMI Sensors API 始终发布原始测量。订阅不会改变硬件 ODR 或任务周期。

每颗 IMU 有 64 条样本缓存，满时丢弃新样本并增加 `overflow`；编码复用 HostComm 线程，每包最多六条，两条流轮流处理。正常无拥塞时，最老样本达到 4 ms 即尝试提交，给 1 ms 的 HostComm 唤醒预算留出余量；不是 USB 主机到达时间保证。离线时不重复发送上次快照。

`TSKPTT_HostComm_RegisterTelemetryEx()` 支持固定订阅周期、校验、会话配置、发送完成和有界 poll 回调。旧注册 API 仍为保留最新值策略。FIFO 成功表示复制入队，完成回调才表示底层发送完成；不代表主机应用已读取。12 个发送槽中 FIFO 最多使用八个，其余可用于命令及其他流量。FIFO 不合并、不覆盖，按入队序号排序。满时返回 `SRN_LINK_ERR_FULL`，生产者自行处理背压。

取消订阅清理该流未发送数据；DTR 下降、USB 断连清理订阅、请求缓存和会话数据。已交给 USB 的缓冲必须等真正完成或 endpoint 关闭确认取消后才回收。重新连接必须重新 HELLO、订阅。旧会话的延后完成不会发送到新会话。所有默认配置禁止 HostLink LOAD/SAVE 并关闭参数自动加载；本次没有适配 EEPROM。

## 线格式

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

需要保存完整逐样本数据时使用新增 `Tools/HostLink/imu_capture.py`，见 [采集说明](../Tools/HostLink/README.md)。它输出双 CSV、原始接收字节、每秒状态与元数据，支持预热/定时停止，断连后停止而不拼接会话。下述 `imu_telemetry.py` 仍为只显示的监视器。

在 SDK 根目录：

```sh
python3 -m venv build/hostlink-venv
build/hostlink-venv/bin/pip install -r Tools/HostLink/requirements.txt
build/hostlink-venv/bin/python Tools/HostLink/imu_telemetry.py \
  --port /dev/cu.usbmodem355C343932333 --duration 600
```

端口必须显式选择 **CDC1**，路径随主机枚举变化；上面的路径是 2026-09-27 本机观测值。CLI 拒绝占用中的端口；CDC 波特率设置不改变 USB 链路速率。每秒显示两颗 IMU 完整最新值、实际接收率、序号缺口、重复与设备计数，每十秒输出采集率/接收率及协议错误。每个正常十秒窗口目标均 ≥990 次/秒；不以配置周期代替测量。`--duration 0` 持续运行，Ctrl-C 取消订阅。重连重新握手订阅；无需打开 CDC0。

状态/速率输出到 stdout，不录制文件。对进程暂停读取的测试可能被主机 USB/TTY 缓冲吸收，未触发 overflow 时不能宣称验证了设备满队列行为。当前固件未进行十分钟长测，由用户继续执行；短测证据见 [Validation.md](Validation.md)。

C 与 Python 共用 `Tests/Services/IMUTelemetry/golden_record.hex`；主机套件覆盖字段字节序、溢出、会话清理和坏帧恢复。实板短测不代替满负载 CAN、传感器校准或实际声光观察。

### 温度与终端显示

`imu_capture.py` 与 `imu_telemetry.py` 默认使用易读状态行，显示两颗 IMU 的实测温度、接收率、样本数、缺序和设备累计溢出/发送失败；离线温度标为旧值。`--json` 保留机器可读终端输出。CSV、原始二进制和状态文件格式不变。SensorBringUp 当前优先控制 LSM 为60°C、LSM失效时回退BMI，CDC0显示实际来源和占空比；先用 monitor 观察温度趋稳，再开始正式静置采集，`--warmup` 只是时间分段，不会自动判定温稳。

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

默认省略`--streams`仍仅两路原始流。终端每秒显示姿态、温度、来源/权重、温稳/保护状态和速率；`--json`附完整状态及每10秒实际发布/接收速率。重连重新握手和订阅，退出取消。`imu_capture.py`继续仅保存双IMU原始CSV，不会隐式改变旧数据结构。
