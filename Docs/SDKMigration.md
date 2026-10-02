# 父项目源码迁移与公共边界

本轮是源码不兼容迁移。旧名称、旧头路径和空兼容入口已删除，不提供别名；父项目与 SDK 一起重新编译。命名与依赖隔离阶段保持既有 COBS/HostLink 编码及硬件格式；后续持久化阶段引入新的 FT24C128A 单份记录，明确不识别旧 24C02 格式，并新增消息号。不能把“命名阶段不改协议”理解为所有历史 EEPROM 都可兼容。已有 USB 描述字符串和调试记录 magic 不因源码命名改变。

## 构建与线程

父项目通过 `sirinos_add_firmware()`，或链接 `SirinEmbedOS::Platform` 接入。应用编译只使用 SDK 公共 Include 和自身配置，不再补充 FreeRTOS、CMSIS、HAL、Board、Source 的路径。专项内核测试的原生依赖只授予测试适配目标，不传给生产应用。

应用保留 `SRNOS_ApplicationConfigure()` 和 `SRNOS_ApplicationMain()`。长期任务提供 `init/step/cleanup`，由 Kernel 创建及管理；不要保留旧 `entry`、手写无限周期循环或统计 hook。运行期通过 `SRNOS_ThreadCreate/RequestStop/Join/Delete` 与 `SRNOS_Delay/DelayUntil` 管理。同步使用 SDK 通知、互斥锁、信号量、队列及事件标志。具体生命周期、超时、ISR 限制和可编译示例见 [Kernel](../Source/Kernel/README.md)、[Minimal](../Examples/Minimal/README.md) 和 [父项目接入](../README.md#父项目接入)。

## 能力迁移

| 原用法 | 当前公开入口与合同 |
|---|---|
| `tskptt_host_comm.h`、直接操作 link context/handler 表 | `Services/HostLink/HostLink.h`，`SRNOS_HostLink*`；setup 上下文不透明，注册启动后冻结。应用只通过注册、发布、控制邮箱和副本快照交互。 |
| `bsp_srn_log.h`、原生 RTOS 返回值、打印 timeout 参数 | `Services/Logging/Log.h`，`SRNOS_LogPrintf(level, source, format, ...)` 返回 `SRNOS_LogResult`。原 timeout 实际未生效，现已删除；运行期有界非阻塞提交，耗尽返回错误，不在调用者同步输出。 |
| `bsp_spi.h` / direct 入口 / CS 或 HAL handle | `IO/SPI/SPI.h`，`SRNOS_SPIDevice` 不透明；固定设备由 Board 装配。通过普通事务接口访问，初始化、GPIO、DMA 和取消恢复实现私有。 |
| `bsp_i2c.h` / direct 入口 | `IO/I2C/I2C.h`，`SRNOS_I2C*` 及 SDK 地址宽度类型；超时使用毫秒。 |
| 未归属命名空间的 CAN router / FDCAN / MCP 供应商头 | `IO/CAN/Router/CAN.h` 与 `IO/CAN/External/ExternalCAN.h`，`SRNOS_CANRouter*` / `SRNOS_ExternalCAN*`；统计返回调用者副本。HAL 与供应商头私有。 |
| `lib_params*`、遥控、Referee、旧设备 adapter | 对应 `Services/Parameters/Parameters.h/.hpp`、RemoteControl/Referee 及 Devices 公共头；C 为 `SRNOS_*`，C++ 为 `SRNOS::Parameters` / `SRNOS::Devices`。无独立空 Referee TX Task。 |
| CMSIS-DSP 矩阵、旧算法全局类/命名空间 | `Algorithms/Math/Matrix.h` 使用 SDK 矩阵描述；算法归入 `SRNOS::Math`、`SRNOS::Control`、`SRNOS::IMU`。矩阵尺寸和数据缓冲由调用者持有，不公开 DSP 类型。 |
| 驱动 init、寄存器、IRQ、scratch、Board 私有头 | 改用 Sensors/INS、EEPROM、灯/蜂鸣器等能力接口。固定资源已经由 SDK 装配，不在父项目二次初始化。 |

逐项名称和路径见 [srnos-renames.json](Migration/srnos-renames.json)。该表用于查找源码迁移，不是运行期别名层；最终声明及参数合同以当前公共头为准。`scoped_symbols` 只作用于所列文件，`removed_symbols` 标出已删除入口；不能把通用局部词替换到整个父项目。表中移入 Source 的项目已成为私有实现，应用应转用上表对应能力。

等待参数以毫秒和现有 `SRNOS_NO_WAIT` / `SRNOS_WAIT_FOREVER` 表示；总线仍有独立的队列/活动保护期限。SPI 显式零期限不启动传输，异步便捷入口显式使用原 5 ms 默认期限；I2C 完成查询零等待可返回 BUSY，此时 buffer 仍由在途事务持有，须继续等待或取消确认。异步 buffer、参数对象和回调上下文的生命周期按公共头约定管理。不能用 `void *`、整数转换或额外 include 把原生句柄穿过公共边界。HostLink 回调不得阻塞 HostComm；长命令在 HostCmd 执行。传感器/总线完成回调可能位于服务线程或 ISR，须按各接口规定选用 SDK ISR 入口。

## 运行行为与资源

CAN RX/TX、HostComm/HostCmd、Indicators/SystemMonitor 保持分离；本轮不压缩默认线程栈和队列。HostComm 每轮 RX 上限 1024 字节，CAN RX worker 上限 32 帧；原生 FDCAN ISR 仍排空硬件 FIFO，需要板端负载验证。积压会继续安排处理，输出和同优先级任务仍有运行机会。SPI、Serial、Indicators 按事件与最近截止点等待，CAN 的外部适配器 tick/poll 合同保留。

HostLink 活动请求表固定 10 项，覆盖排队、执行、完成及待发送结果。活动请求不因 2 秒缓存期限而过期；执行前表满返回 BUSY，同身份重试不重复执行。完成重放缓存仍为 4 项/2 秒。HostComm 固定持有一个待发送结果，发送成功或会话失效才取下一项。断连隔离旧结果，已经执行的业务不回滚。交付回调统一在 HostComm 释放保护后执行。

SPI/I2C 采用预留、保护外复制、发布；复制中的槽不能被派发或复用，取消按生命周期收敛。Serial 单次 Read/Write/Flush 的期限包含锁竞争；Transceive 将发送准备期限和发送完成后的响应期限分开。运行期日志池/队列耗尽只返回并计数，启动输出和 Fault 仍有独立合同。

新增资源是固定 D1 状态及同步控制块：Serial 通道数组每槽预留两个静态二值信号量控制块，共 12 份（含 NONE 槽，最多初始化 10 个）；HostLink 活动身份表及一份待发送结果；CAN4 增加一个失败重试期限。没有新增常驻线程或运行期动态分配。静态增加量、完整 ELF 的 D1/D2/D3、heap、TLS 与默认预算见 [本轮验证](Validation/SRNOSBoundary-20260928/README.md)。`SRNOS_SystemHeapBudget()` 是 RTOS 启动堆预算，不能把静态 D1 增量当成新增 heap 分配。

## 命名例外与验证边界

仅保留有外部合同的原名：第三方原始源码/许可证、CubeMX 生成符号、FreeRTOS/CMSIS/HAL 所需入口和 USB/Microchip 适配 ABI（如 `HAL_*Callback`、`USBD_LL_*`、`DRV_SPI_TransferData`，按实际定义文件逐项检查）。历史来源清单及实测证据不改写。`SRNOS_` 是 SDK 线程名保留前缀，父项目线程名字不得使用它。

主机行为测试、独立公共头检查、父项目集成、Debug/Release 构建证明源码和静态合同。此次没有烧录；同负载 CPU/唤醒次数/临界区、INS/CAN/传感器与 CDC 拥塞仍须板测。循环 hook ≤1 µs、统计增量 ≤1 个百分点、Monitor 平均 CPU ≤0.1%、监测临界区 ≤10 µs 均不得用主机结果替代。

## 单份持久化与两类变量迁移

删除旧24C02接口和格式、`Param<T>`及access开关；使用`Persistent<T>(stable_id,name,default,min,max)`声明可写业务参数，应用配置非零parameter_application_id，类型/范围/默认值不可缺。只读运行量使用`Telemetry::Value<T>`并Publish带实际采样时间的缓存，不注册为参数；旧HostLink原始value_ptr/getter源入口移除。C代码使用对应注册/值接口，不包含RTOS/HAL。

默认启动加载和业务SAVE/LOAD改为开启；原EEPROM内容不自动迁移或格式化。单份写入中断可丢失该记录，启动回默认或未校准降级，运行LOAD失败保留现值。PCB序列号20字节ASCII，专用工厂开关写入。SOURCE V1线编码保留，V2采用0x10–0x14以避开已有IMU/INS状态0x08/0x09。新Storage/Telemetry和校准公共接口及最新测试边界见根README。

静态校准现通过 CONFIG 0x09 手动触发，0x0A 查询，保留加速度系数并下次启动应用；它不通过参数 WRITE/SAVE 伪装成普通业务变量。当前线上消息与兼容范围统一见 [HostLink](HostLink.md)，落盘布局与失效处理见 [Storage](Storage.md)。
