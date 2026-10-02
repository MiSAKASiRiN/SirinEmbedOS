# 日志、HostLink 与业务参数

系统配置在 `osKernelInitialize()` 前完成声明、校验与冻结。`SRNOS_LoggingPrepare` 仅绑定输出并保存配置；`SRNOS_HostLinkPrepare` 注册协议与应用声明，随后拒绝追加命令、遥测源和控制通道。两者不创建 RTOS 资源，不启动 USB。`*Start()` 在 Kernel Ready 后创建原有服务线程。`SRNOS_USBStart()` 从任务上下文启用已初始化的 PCD；库、配置与双 CDC 不由 CubeMX 管理。

- CDC0：ASCII 日志，固定容量队列、断连/DTR处理和启动文本重放。
- CDC1：SRN-Link/HostLink，保留 framing、CRC、QoS、遥测和 deferred worker。
- UART 日志：显式配置并占用一个 Serial channel，复制整行后有界入队；不复位USART1、不使用Emergency输出，也不会与另一协议争用端口。未绑定时不占任何UART。
- `printf` 使用正常日志 sink；生成 `_write` 不传播每字符提交失败，不能作为可靠交付接口。需要准确状态时使用日志/Serial提交API。`getchar`不从协议UART窃取数据。

应用的 HostLink setup 仅注册业务命令、业务参数和遥测；SDK不注册硬件路由、CAN模式、UART角色、服务开关、线程容量或IMU选择的远程修改入口。遥测订阅是通信会话行为，不重配底层系统。

父项目使用两种独立声明，目录在启动期冻结，名称与对象须具有静态生命周期：

```cpp
SRNOS::Parameters::Persistent<float> speed_kp{0x1001, "speed.kp", 1.0f, 0.0f, 30.0f};
SRNOS::Telemetry::Value<int32_t> encoder{"motor.encoder", "count", 1000};
```

`Persistent<T>` 提供稳定 ID、默认值和范围，只声明业务数值。父项目在 `SRNOS_ApplicationConfigure()` 设置非零 `parameter_application_id` 及 `parameter_compatibility_version`。WRITE/Set 只改 RAM，应用在周期边界使用一致快照；SAVE 固定本次代次，期间继续 WRITE 不会被标为已保存。启动无效记录采用默认值；运行 LOAD 无效时保留当前 RAM。旧 `Param<T>`、通用访问标志和 24C02 格式已经删除，不保留兼容别名或自动迁移。

`Value<T>::Publish(value, sample_time_us)` 仅在任务上下文更新固定缓存，首次发布前无效；它不可远程写入或保存，不唤醒 HostComm。SOURCE V2 支持 32 源、单源最高 1 kHz、默认总预算 8000 标量/秒，采用最新值。V1 编码保留，V2 使用 TELEMETRY `0x10..0x14`，避免与 IMU/INS 状态消息冲突。

Storage 统一拥有 FT24C128A 分区、CRC/提交页和单操作互斥，不增加线程。默认启动在 2 s 总期限内依次解析身份、IMU 校准及参数，随后放行应用；默认启用 HostCmd SAVE/LOAD，本地存储不依赖 HostLink。每类记录仅一份，写入中断可能使该记录失效，不保证恢复上一版；启动不写回默认值。详细状态和 PCB 身份由 CONFIG `0x06/0x07` 查询。序列号只能在启动工厂开关启用后由本地接口写入，默认关闭。接口及示例见 [Storage](Storage/README.md)、[Parameters](Parameters/README.md) 和 [Telemetry](Telemetry/README.md)。

固定 AL94 双CDC的 class context 使用静态数组，EP0 的 SET_CONFIGURATION / Init 不调用分配器。SDK 的 `usbd_conf.h` 不提供通向 libc 的 `USBD_malloc/free` 别名；增加其他USB class前必须复核中断上下文、存储所有权及资源预算。

验证脚本在 `Tests/Services/`，覆盖原HostLink/codec/参数协议、参数冻结和访问限制、并发batch/snapshot、持久化损坏，以及双CDC回调隔离、DTR/断连/日志队列。USB枚举、PCD时序、实际UART吞吐仍需板端验证。

双 IMU FIFO 遥测使用新增 `RegisterTelemetryEx` 和初始化冻结的样本观察回调，不增加线程。每颗 64 条静态队列、每包最多六条，HostComm 负责编码与发送；CDC1 会话变化清理订阅/数据，USB 尚引用的缓冲延后回收。父项目标量遥测使用独立目录的最新值缓存。SensorBringUp 关闭周期栈扫描（UINT32_MAX 水位表示未测量）。完整协议、接口合同和 CLI 见 [HostLink.md](../../Docs/HostLink.md)。

正式 INS 默认自动注册五条传感器/姿态遥测流，原始观察回调通过Kernel接入，不让设备驱动依赖通信服务。统一温控、坐标和多读者API见 [INS合同](../../Docs/INS.md)。

## 板级与服务边界

Indicators 服务持有启动提示/心跳及原 worker；USB 服务持有 CDC 装配和启动生命周期。固定硬件初始化、PCD 接入和 IRQ 接线由 Board/Support 提供，见各模块 README。

## SystemMonitor

ADC和任务统计由SystemMonitor统一提供1 Hz快照，Low/4 KiB线程替代原Diagnostics；Indicators不变。HostLink注册0x0001/0x0002最新值遥测，详见 [模块合同](SystemMonitor/README.md)。移除HostLinkPrepareEx的stack_scan参数和通信线程周期扫栈，历史栈字段用UINT32_MAX标记未测量。

## 托管执行

SDK 服务通过 Kernel 统一框架和 SRNOS_ 名称执行，初始化及普通等待在循环统计外；原优先级/栈/通知/批次保留。HostLink 注册容量为 12，新增 0x0003 循环明细，原 0x0001/0x0002 保持。父项目通过公开 Kernel API 创建周期/事件/单次任务，配置冻结不禁止有界运行期对象生命周期；见 Kernel README。

## 父项目公共边界

应用包含 SDK 公共头，使用 SRNOS_Log、SRNOS_HostLink、SRNOS_Parameters 等接口；Prepare/Start、运输层、worker、队列和静态池是内部装配。注册上下文不透明，禁止绕过冻结直接修改 link handler。运行期日志有界非阻塞，HostLink 长命令身份在执行前保留，断连/拥塞按独立会话和固定待发送结果处理。详见 [源码迁移](../../Docs/SDKMigration.md)。

INS 的静态校准通过 CONFIG `0x09` 手动触发，HostCmd 采集原始快照、校验并保存单份 EEPROM，HostComm `0x0A` 只读进度；不新增线程或在 Sensors/INS worker 内累计校准数据。见 [INS](INS/README.md#手动静态校准)。

模块使用和交互入口统一见 [文档导航](../../Docs/README.md)，记录格式见 [EEPROM 设计](../../Docs/Storage.md)，帧/消息号见 [HostLink 协议](../../Docs/HostLink.md)。
