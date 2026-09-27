# 日志、HostLink 与业务参数

系统配置在 `osKernelInitialize()` 前完成声明、校验与冻结。`SRNOS_LoggingPrepare` 仅绑定输出并保存配置；`SRNOS_HostLinkPrepare` 注册协议与应用声明，随后拒绝追加命令、遥测源和控制通道。两者不创建 RTOS 资源，不启动 USB。`*Start()` 在 Kernel Ready 后创建原有服务线程。`SRNOS_USBStart()` 从任务上下文启用已初始化的 PCD；库、配置与双 CDC 不由 CubeMX 管理。

- CDC0：ASCII 日志，固定容量队列、断连/DTR处理和启动文本重放。
- CDC1：SRN-Link/HostLink，保留 framing、CRC、QoS、遥测和 deferred worker。
- UART 日志：显式配置并占用一个 Serial channel，复制整行后有界入队；不复位USART1、不使用Emergency输出，也不会与另一协议争用端口。未绑定时不占任何UART。
- `printf` 使用正常日志 sink；生成 `_write` 不传播每字符提交失败，不能作为可靠交付接口。需要准确状态时使用日志/Serial提交API。`getchar`不从协议UART窃取数据。

应用的 HostLink setup 仅注册业务命令、业务参数和遥测；SDK不注册硬件路由、CAN模式、UART角色、服务开关、线程容量或IMU选择的远程修改入口。遥测订阅是通信会话行为，不重配底层系统。

`omx::params::Param<T>` 默认只读。允许远程修改的业务数值必须显式声明：

```cpp
omx::params::Param<float> speed_kp("speed.kp", 1.0f, 0.0f, 30.0f,
                                  PARAMS_MUTABLE_BUSINESS);
```

参数对象及名称必须存活到固件结束，可静态构造或在启动配置阶段创建。`SRNOS_ParametersFreeze()` 关闭注册并检查既有注册失败；它不初始化I2C、也不访问EEPROM。冻结后参数目录和访问权限不变，可写业务数值仍可在约束范围内更新。`Param::Set()` 是应用本地数值操作，HostLink/C写入接口另外执行显式访问检查；不应使用参数对象保存系统结构配置。

标量 `Get/Set`、二进制batch和 `params_snapshot()` 使用短临界区；批量写入先全量验证再应用，snapshot返回调用者拥有的一致副本。HostLink READ也使用同一快照接口。`params_load/save`只在任务上下文调用，持久化LOAD先完整校验，失败保留现值，只应用可写业务值；锁不覆盖EEPROM传输。系统默认 `load_parameters=false`，不自动访问尚未适配的新EEPROM；显式启用时在应用线程启动阶段、USB启动前尝试加载，显式声明的应用线程等待该阶段结束。`host_link_storage_enabled=false` 在启动时禁止 HostLink LOAD/SAVE；默认同样关闭；明确启用的主机存储请求由 HostCmd 线程执行。原EEPROM格式、布局hash与0x40起始位置保留。

固定 AL94 双CDC的 class context 使用静态数组，EP0 的 SET_CONFIGURATION / Init 不调用分配器。SDK 的 `usbd_conf.h` 不提供通向 libc 的 `USBD_malloc/free` 别名；增加其他USB class前必须复核中断上下文、存储所有权及资源预算。

验证脚本在 `Tests/Services/`，覆盖原HostLink/codec/参数协议、参数冻结和访问限制、并发batch/snapshot、持久化损坏，以及双CDC回调隔离、DTR/断连/日志队列。USB枚举、PCD时序、实际UART吞吐仍需板端验证。

双 IMU FIFO 遥测使用新增 `RegisterTelemetryEx` 和初始化冻结的样本观察回调，不增加线程。每颗 64 条静态队列、每包最多六条，HostComm 负责编码与发送；CDC1 会话变化清理订阅/数据，USB 尚引用的缓冲延后回收。原遥测注册仍保留最新值。SensorBringUp 关闭周期栈扫描（零水位表示未测量，不表示剩余栈为零）。完整协议、接口合同和 CLI 见 [HostLink.md](../../Docs/HostLink.md)。

正式 INS 默认自动注册五条传感器/姿态遥测流，原始观察回调通过Kernel接入，不让设备驱动依赖通信服务。统一温控、坐标和多读者API见 [INS合同](../../Docs/INS.md)。
