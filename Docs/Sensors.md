# 2613 传感器与共享总线合同

驱动提供识别、配置回读、持续采样、状态与统计。融合和安装坐标变换由独立 [INS 服务](INS.md) 提供；磁航向、高度和用户标定尚未实现。板端证据及未通过项见 [Validation.md](Validation.md)。2026-09-27 短测已识别四颗器件，双 IMU 持续采样；较早的 LSM 离线记录保留在历史验证条目中，不代表当前结果。精度及长时间稳定性仍未验收。

## 配置与公开 API

公共头为 `SirinEmbedOS/Devices/Sensors.h`。在 `SRNOS_ApplicationConfigure()` 中设置 `config->sensors`，系统检查并复制后冻结；恢复仅用冻结配置。默认四颗传感器采样及正式 [INS](INS.md) 服务启用。BMI 始终发布未用户校准的器件坐标数据，不再有 RAW/legacy 分支。SensorBringUp 设置 `ins.enabled=false`，保留原始采样与统一温控。加热统一由 INS worker 控制：`heater_enabled=true`、`heater_target_c=60` 默认，目标允许20..75°C。反馈 LSM 优先、BMI 回退；低于目标10°C满功率，附近连续 PI，80°C保护及75°C以下5秒恢复详见 INS 合同。`SRNOS_SensorGetHeaterStatus` 仍提供一致状态副本。

`imu_observer` / `imu_observer_user` 在配置阶段声明并冻结，完整 BMI/LSM 样本发布后调用一次。回调参数仅在调用期间有效，需自行复制；回调在对应任务（BMI High5、LSM SPI dispatcher High6）执行，禁止等待、打印或编码整包 USB 数据。SDK 提供静态缓存适配 `SRNOS_IMUTelemetryObserve`，接入见 [HostLink.md](HostLink.md)。

| 器件 | 固定绑定 | 首版配置 | 任务周期 |
|---|---|---|---|
| BMI088 | SPI4，ACC PE4 / GYRO PA0 | 保持已有量程、ODR | 默认 1 ms |
| LSM6DS3TR-C | SPI4，PD5，PD6 保留但不启用数据中断 | ±4g、±2000 dps、1.66 kHz、BDU、自增、无 FIFO | 默认 1 ms |
| LIS2MDL | I²C4，7 位地址 0x1E | 连续 100 Hz、BDU、芯片温度补偿 | 10 ms |
| SPA06-003 | I²C4，7 位地址 0x76 | 压力/内部温度 16 Hz，过采样 P8/T1 | 10 ms |

原始采样允许 BMI/LSM 周期 1–100 ms，环境线程10–100 ms；正式 INS 要求六轴1 ms，默认1/1/10 ms。没有运行时 ODR 或总线改绑接口，配置不进入 HostLink 参数表。I²C 地址只在 HAL 边界左移一次。

`SRNOS_SensorGetStatus()`、`SRNOS_SensorGetIMU()`、`SRNOS_SensorGetMagnetic()`、`SRNOS_SensorGetPressure()` 在任务上下文复制快照，返回布尔值表示参数是否合法。**成功返回不代表数据有效**：调用者还须检查 `header.state == SRNOS_SENSOR_READY` 及 `valid_fields`。单份快照通过短临界区保证一致；多次 getter 不构成跨设备同时采样。

- 原始数值和物理值采用各传感器自己的轴向。加速度 m/s²、角速度 rad/s、磁场 µT、压力 Pa、温度 °C。
- 起止时间来自 DWT 单调时基，表示主控事务采集范围，不是器件内部转换时间。LSM 读取最新值，未保存每个 1.66 kHz 内部样本。
- 序号仅在发布有效样本时递增。离线/过期时保留上次值和时间，清空有效位。BMI/LSM 超过五个配置周期、LIS 超过 100 ms、SPA 超过 250 ms 未更新时对外标记 STALE。
- 状态区分 DISABLED、INITIALIZING、READY、OFFLINE、UNSUPPORTED、CONFIG_ERROR、STALE。ID 为 0x00/0xFF 视为离线，其他未知 ID/版本明确拒绝配置；SPA 仅接受产品/版本 1/1。
- 初始化按寄存器步骤和有界 reset 等待推进，失败每秒重新尝试。单个器件缺失不触发系统 Panic；正式 INS 启动3秒内两个六轴都不可用会 Panic。SPI/I²C 无法确认停止时隔离整条总线，须复位处理，不能承诺带硬件故障的总线仍可访问其他器件。

LIS 多字节读设置 SUB[7]，磁场六字节与温度两字节分别读取；实板不采用从磁场起始地址直接连续读八字节的方法。SPA 读取本颗芯片全部 21 字节工厂系数并符号扩展，包含 c31/c40；补偿属于基础驱动，不是用户校准。

## SPI4 所有权与调度

所有同步/异步调用共用 IO 层唯一队列和 DMA 通路。固定八个静态请求槽：四个 IMU、四个普通事务；同类 FIFO。正在传输的命令及数据不抢占且保持同一 CS；最多连续四个 IMU 事务后，如普通队列有请求，执行一个普通事务。MCP 每笔寄存器事务重新参与仲裁，不跨 CAN drain 循环占用 SPI。

- 接纳时复制 TX；单事务最多 512 B，包括命令与 dummy 字节。调用返回后 TX 可复用，RX/设备描述/回调 context 必须存活到完成通知或取消确认。
- 异步 `SRNOS_SPI_OK` 仅表示已接纳。同步接口等待完成信号；发生等待超时后仍等到 dispatcher 确认取消才归还 RX 所有权。旧 direct 名称也遵循复制及 512 B 合同。
- `SRNOS_SPICancel()` 返回 BUSY 时仍持有 buffer/context；重复调用直到 OK 才可销毁或复用。它也覆盖正在执行的回调。
- 回调在 High6 dispatcher 运行，只可更新状态、提交短异步后续事务或通知任务；不得等待、延时或调用同步 SPI。全部提交/取消 API 仅限任务上下文。
- IMU 队列期限 1 ms；活动 DMA 超时检测上限 5 ms。后者是故障阈值，不是正常采样预算，也不是 HAL abort 的墙钟时间保证。恢复须确认 DMA/SPI 停止、清除 pending IRQ 后才释放 CS 并继续；abort 失败隔离总线，不放行另一个 CS。
- 每颗 IMU 最多一轮采样在途，慢事务只记超期并跳过发起，不积压或追赶。BMI 仍由原采样线程持有，DMA完成通知唤醒后在当前周期发布，不再将已完成样本拖至下一任务周期；单周期只发起一轮，超时保留在途状态并跳过后续发起。

固定 10 MHz；BMI/MCP mode 0，LSM mode 3。模式仅在 CS 全部释放后切换，Board glue 在拉低新 CS 前建立正确 SCK 空闲电平。LSM 不使用 BMI 的专有 dummy-byte 读时序。IO DMA staging 位于 D2，I²C4 BDMA staging 位于 D3；应用/任务栈不作为 BDMA 缓冲。

`SRNOS_SensorGetBusStatistics()` 提供 BMI 合并统计和 LSM 统计；BMI 两个 CS、MCP 独立统计由验证快照保存。排队/传输时间桶上界为 25/100/250/500/1000 µs，最后一桶更大；任务周期桶为 500/1000/1500/10000/20000 µs/更大。累计最大值包含初始化和调试暂停，实际采样率应使用不暂停的 10 秒窗口。

## 线程、内存与 I²C

SPI dispatcher High6/3 KiB；BMI High5/2 KiB、LSM High5/2 KiB；MCP RX/TX 各 High4/4 KiB。LIS/SPA 共用 Normal/2 KiB 线程，等待芯片转换时释放 I²C，不增加每器件线程。新增线程与八个完成信号已计入保守启动预算；请求及采样 buffer 静态分配，采样循环不分配内存。RTOS 线程/信号资源仍在启动时从 heap_5 创建。

I²C4 复用原 High6/3 KiB 回调线程作为唯一分发者，LIS/SPA 各有一个保留槽，EEPROM 等后台有两个槽。周期类优先，最多连续四个周期事务后放行一个等待后台事务。活动事务不可抢占、单笔最多 64 B；EEPROM 自行将读写/校验限制为 32 B，内部写周期不占总线。环境任务使用 30 ms 完成信号量等待，另一个设备占用总线不再使提交立即 BUSY。所有权、取消、期限与统计见 [I2C 模块说明](../Source/IO/I2C/README.md)。共享负载板测见 [验证记录](Validation/I2C-20260928/README.md)，板端断线/卡 SDA/掉电恢复仍未验收。

## 来源

新驱动按数据手册实现，未引入新的嵌套仓库或上游库：

- [ST LSM6DS3TR-C 数据手册](https://www.st.com/resource/en/datasheet/lsm6ds3tr-c.pdf)
- [ST LIS2MDL 数据手册](https://www.st.com/resource/en/datasheet/lis2mdl.pdf)
- [SPA06-003 原厂手册镜像](https://files.seeedstudio.com/wiki/Grove-Barometer_Sensor-SPA06-003/res/Datasheet.PDF)
