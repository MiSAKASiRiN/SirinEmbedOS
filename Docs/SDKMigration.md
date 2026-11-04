# 父项目源码迁移与公共边界

## VT02/VT12 图传接入

`SRNOS_RemoteControlConfig` 增加 `vt02_port`，SDK 与父项目须整体重编译；未绑定默认关闭。切换旧图传时将原端口 Serial 改为 115200/8N1、普通非反相 RX，清除该端口的 vt03_port 绑定，改设 vt02_port。Input 来源名为 `vt02`，仅有键鼠；不能沿用 VT03 模拟通道与 C/N/S 逻辑。0302 的全部 30 B 经 `SRNOS_MESSAGE_VT02`（新增 transport 位）、service/type=0x22/1、endpoint=Serial port 投递，需显式注册 Message 路由。常规 referee_port 与 VT03 VideoLink 不是这个入口。见 [VT02](../Source/Services/RemoteControl/VT02/README.md)。

TX 使用同一内置绑定：Serial 显式设置 `tx_enabled=true`，任务调用 `SRNOS_VT02Send` 和可选 `SRNOS_VT02Flush`。不再申请第二个 Serial 句柄，`SRNOS_MessageSend` 的 VT02 目的端不受支持；VT03 `VideoLink` 的周期聚合/遥测格式也不适用于该入口。完整配置、超时/栈合同及示例见 [VT02/VT12 双向接入](VT02VT12.md)。现有纯 RX 应用可继续将 TX 关闭。

## 源码分发目录调整

Git 分发不包含测试套件、实机验证归档和 BringUp 示例。本地测试及其 BringUp 依赖由忽略规则管理，不参与提交。公开 SDK 的独立构建可选择 Minimal、InputMessages 或 VT02VT12；本地专用观测目标由私有测试父工程构建。内核测试适配函数也放在私有测试工程内。生产 `SirinEmbedOS::Platform`、公共 API 和 `sirinos_add_firmware()` 保持原合同。

已有父项目可在本地私有测试目录存在时复用测试桩；公开父项目不能假设 SDK 的 Git 检出包含这些文件。现场资料和历史 Git 对象不属于公开源码快照；已有私有仓库的历史发布需单独检查。

## 裁判发送初始化

删除旧应用中的 `SRNOS_RefereeTxInit()` 调用，无需替代初始化入口。启动配置仍设置 `referee_port`，SDK 完成绑定及 Serial 初始化后，使用 `SRNOS_RefereeTransmit()` 或 `SRNOS_RefereeTxWrite()` 发送。

## Blackbox 接入

`SystemConfig` 新增 `blackbox`，SDK/应用整体重编译。默认关闭；启用时在配置阶段声明
队列深度并核对 `SystemHeapBudget` 和实际 map。父项目仅包含 `Services/Blackbox.h`，
使用 Append、RequestRead/PollRead/CancelRead、GetSnapshot；不得创建 NOR worker 或
绕过 SDK 访问 OSPI。公开接口不执行 Flash I/O，OK 入队与记录持久化是不同状态。
接口与固定磁盘格式见 [Blackbox](../Source/Services/Blackbox/README.md)。

## SirinRMT 平面速度扩展

新增 kind=6 PLANAR_VELOCITY，v1 的其他类型含义和 30 B 长度不变。InputDescriptor 新增 planar_velocity 能力，InputFrame 新增 update_planar_velocity / planar_velocity，InputSnapshot 新增速度、接收时间、有效位及 motion_kind / motion_epoch；SDK 与应用须整体重编译。旧固件拒绝 kind=6，旧 CHANNELS 发送端仍可使用。

消费者不能将速度塞入归一化通道；须独立检查有效性、时间、来源 epoch 和 motion_epoch。类型变化清除旧 ENABLE 与运动有效位，应用据此撤权、重新建流/使能，DISABLE 的独立锁存身份仍须消费。速度上限及物理接收器否决策略留在父项目；通用 codec 仅检查有限 IEEE754 float32。Python 新增 planar_velocity() / --velocity，泛用 CLI 允许最高 100 Hz。

`System.h` 新增 `SRNOS_SystemBootId()`，应用可在启动后读取非零随机 64 位启动标识，用于隔离复位前后时间/序号；无既有 API 删除。UDP Python 使用 `pack_raw/unpack_raw`，串口继续 `pack/unpack`。

父项目仅依赖 SDK 公共头，并与 SDK 使用同一版本整体编译；旧名称、旧头路径和空兼容入口不提供别名。SirinLink 线协议、EEPROM 记录和源码 API 是不同兼容边界：FT24C128A 单份记录不识别旧 24C02 格式，不能仅因 COBS 外层未变就推断数据兼容。当前协议见 [SirinLink](SirinLink.md)，记录布局见 [Storage](Storage.md)。

## SirinLink 源码接入

通信服务的公开路径统一为 `Services/SirinLink/`，入口为 `SirinLink.h`；类型和函数使用 `SRNOS_SirinLink*`，宏使用 `SRNOS_SIRIN_LINK_*`，SystemConfig 使用 `sirin_link_enabled/storage_enabled/setup/user`。参数适配路径为 `Services/Parameters/SirinLink/`，Python 工具位于 `Tools/SirinLink/`。应用、测试和工具引用须同步迁移，SDK 不提供旧头、符号或路径别名。

SirinLink 外层仍为 V2，magic、服务编号、COBS、CRC 和消息布局保持原格式；SirinRMT 载荷版本仍为 1。HostComm/HostCmd 是既有传输与工作任务名。历史验证记录与迁移哈希用于读取当时版本，不代表当前源码路径。

## 输入与通用消息迁移

删除 `RemoteControl/RemoteControl.h`、`SRNOS_RC_*`、旧活动来源/优先级/idle/deadzone/post_map_hook 配置和相应全局 Watch。DR16 解码器仍保留。应用包含 `Services/Input.h`，用 InputFind(name) 与 InputReadSnapshot 读取各来源，用 InputReadEvent 的独立 cursor 消费按键边沿/相对增量；来源仲裁、开关意义、归一化后的控制映射由父项目实现。不要把旧固定五通道索引直接套到新来源；布局见 [RemoteControl](../Source/Services/RemoteControl/README.md)。

W.Bus 可设置 remote.wbus_port 由 SDK 发布 wbus 来源，校准填入 remote.wbus_channels；若保留应用独立 Init/Poll，就不要同时声明内置绑定。DR16/VT03/W.Bus 公共对象结构变化，SDK/应用整体重编译。

EJOY 改为 SirinRMT v1，magic 为 SRMT，按三种输入量分页发送；不提供旧 wire 别名。移除旧 VideoLinkCommand/VideoLinkReceive 全局消费队列。自定义业务按 service/type 注册 Message 路由；图传下行使用 SM 紧凑封装或 SRMT 原生载荷，UDP 使用 raw V2，HostComm/消息 UART 使用 V2 COBS。输入模拟只占一个服务，其他消息不更新遥控状态。迁移发送端需使用 [SirinRMT wire 合同](../Source/Services/Input/README.md#sirinrmt-v1)。

SirinRMT ENABLE 为 kind=4、16 B 零 body，沿用全部 kind 共享的 session/sequence 检查。新增 `InputFrame.request_enable` 及 `InputSnapshot.enable_sequence/enable_ms/enable_epoch`，需要整体重编译。应用按序号变化消费一次请求，同时核对 epoch、新鲜度和自己的安全门；阻塞期间也消费，避免稍后自动使能。旧 CHANNELS/BUTTONS/RELATIVE 不隐含请求，INVALIDATE/恢复使旧请求失效。SDK 不直接驱动执行器；接收方必须自行决定是否支持无实体遥控的使能。发送工具 `--enable` 默认关闭且不自动重发。

DISABLE 为 kind=5、16 B 零 body。新增 `InputFrame.request_disable` 及 `InputSnapshot.disable_sequence/disable_ms/disable_epoch`；该请求保留输入字段及有效性，清除此前 enable_epoch。应用按独立计数消费，DISABLE 与 ENABLE 同周期到达时先停机并消费两者；普通数据不能触发重新使能。快照结构扩展要求整体重编译，不改变 SRMT v1 线版本。

新增 SystemConfig.messages 和 remote.sirinrmt_transports。所有 Message/Input 注册在 ApplicationConfigure/bind_channels 完成；bind_channels 现在早于 SirinLinkPrepare，随后冻结通用注册表并装配 HostComm EVENT handler，sirin_link_setup 不再注册 Input/Message。同一消息服务的 SirinLink handler 不要两处重复注册。

VideoLink 仍默认关闭；开启时 VT03 UART 必须 RX/TX 全双工。Telemetry handle 可在 ApplicationConfigure 注册，再在 bind_channels 调用 VideoLinkRegisterTelemetry；运行期只发布数值或读取自己的 Message 队列。图传配置、消息封装和新 rx_messages 统计见 [VideoLink](../Source/Services/VideoLink/README.md)。

本 SDK 提供 [InputMessages](../Examples/InputMessages/README.md) 完整迁移例。依赖旧 API 的父项目必须先完成上述源码迁移才能重新构建，不能只更新子模块指针。

## NTP 与绝对时间

`SRNOS_NetworkConfig` 增加 `ntp`，`SRNOS_NetworkStatus` 增加 NTP 诊断结构，使用方
需与 SDK 整体重编译。默认配置和零初始化仍关闭 NTP；不改变 UDP 端口或已有时钟。
启用时指定可信服务器 IPv4 和 `poll_interval_ms`（0 为单次，非零为 15000..86400000）。
新公共头 `Services/Clock.h` 提供有效性明确的 Unix 微秒和一致的 uptime/Unix 快照。
仅绝对时间映射接受校正，`SRNOS_TimeMs/TimeUs` 保持单调时基语义；消费者使用
absolute_valid/holdover/generation，不将未授时的 uptime 当成 Unix 时间。见 [Clock](../Source/Services/Clock/README.md)。

## 构建与线程

父项目通过 `sirinos_add_firmware()`，或链接 `SirinEmbedOS::Platform` 接入。应用编译只使用 SDK 公共 Include 和自身配置，不再补充 FreeRTOS、CMSIS、HAL、Board、Source 的路径。专项内核测试的原生依赖只授予测试适配目标，不传给生产应用。

应用保留 `SRNOS_ApplicationConfigure()` 和 `SRNOS_ApplicationMain()`。长期任务提供 `init/step/cleanup`，由 Kernel 创建及管理；不要保留旧 `entry`、手写无限周期循环或统计 hook。运行期通过 `SRNOS_ThreadCreate/RequestStop/Join/Delete` 与 `SRNOS_Delay/DelayUntil` 管理。同步使用 SDK 通知、互斥锁、信号量、队列及事件标志。具体生命周期、超时、ISR 限制和可编译示例见 [Kernel](../Source/Kernel/README.md)、[Minimal](../Examples/Minimal/README.md) 和 [父项目接入](../README.md#父项目接入)。

## 最小迁移示例

以下替换直接调用原生 RTOS 创建周期任务的写法。示例只创建一个应用任务，一个固件只实现一份配置入口：

```c
#include <SirinEmbedOS/Kernel/System.h>

static SRNOS_StepResult app_step(void *argument, const SRNOS_ThreadWake *wake)
{
    (void)argument; (void)wake;
    /* 原循环的一轮业务；周期等待由 Kernel 负责。 */
    return SRNOS_STEP_CONTINUE;
}

void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config)
{
    config->threads[0] = (SRNOS_ThreadConfig){
        .name = "AppControl", .step = app_step,
        .stack_bytes = 4096, .priority = 24,
        .mode = SRNOS_TASK_PERIODIC, .period_ms = 2
    };
    config->thread_count = 1;
}
```

已有任务列表时协调索引而不是覆盖；需要停止、删除句柄的任务使用运行期 ThreadCreate。不要在 `app_step` 再保留永久循环或周期 Delay；init/cleanup 及停止超时语义见 [Kernel](../Source/Kernel/README.md)。

## 能力迁移

| 原用法 | 当前公开入口与合同 |
|---|---|
| `tskptt_host_comm.h`、直接操作 link context/handler 表 | `Services/SirinLink/SirinLink.h`，`SRNOS_SirinLink*`；setup 上下文不透明，注册启动后冻结。应用只通过注册、发布、控制邮箱和副本快照交互。 |
| `bsp_srn_log.h`、原生 RTOS 返回值、打印 timeout 参数 | `Services/Logging/Log.h`，`SRNOS_LogPrintf(level, source, format, ...)` 返回 `SRNOS_LogResult`。原 timeout 实际未生效，现已删除；运行期有界非阻塞提交，耗尽返回错误，不在调用者同步输出。 |
| `bsp_spi.h` / direct 入口 / CS 或 HAL handle | `IO/SPI/SPI.h`，`SRNOS_SPIDevice` 不透明；固定设备由 Board 装配。通过普通事务接口访问，初始化、GPIO、DMA 和取消恢复实现私有。 |
| `bsp_i2c.h` / direct 入口 | `IO/I2C/I2C.h`，`SRNOS_I2C*` 及 SDK 地址宽度类型；超时使用毫秒。 |
| 未归属命名空间的 CAN router / FDCAN / MCP 供应商头 | `IO/CAN/Router/CAN.h` 与 `IO/CAN/External/ExternalCAN.h`，`SRNOS_CANRouter*` / `SRNOS_ExternalCAN*`；统计返回调用者副本。HAL 与供应商头私有。 |
| `lib_params*`、遥控、Referee、旧设备 adapter | 对应 `Services/Parameters/Parameters.h/.hpp`、RemoteControl/Referee 及 Devices 公共头；C 为 `SRNOS_*`，C++ 为 `SRNOS::Parameters` / `SRNOS::Devices`。无独立空 Referee TX Task。 |
| CMSIS-DSP 矩阵、旧算法全局类/命名空间 | `Algorithms/Math/Matrix.h` 使用 SDK 矩阵描述；算法归入 `SRNOS::Math`、`SRNOS::Control`、`SRNOS::IMU`。矩阵尺寸和数据缓冲由调用者持有，不公开 DSP 类型。 |
| 驱动 init、寄存器、IRQ、scratch、Board 私有头 | 改用 Sensors/INS、EEPROM、灯/蜂鸣器等能力接口。固定资源已经由 SDK 装配，不在父项目二次初始化。 |

逐项名称和路径见 [srnos-renames.json](Migration/srnos-renames.json)。该表用于查找源码迁移，不是运行期别名层；最终声明及参数合同以当前公共头为准。`scoped_symbols` 只作用于所列文件，`removed_symbols` 标出已删除入口；不能把通用局部词替换到整个父项目。表中移入 Source 的项目已成为私有实现，应用应转用上表对应能力。

等待参数以毫秒和现有 `SRNOS_NO_WAIT` / `SRNOS_WAIT_FOREVER` 表示；总线仍有独立的队列/活动保护期限。SPI 显式零期限不启动传输，异步便捷入口显式使用原 5 ms 默认期限；I2C 完成查询零等待可返回 BUSY，此时 buffer 仍由在途事务持有，须继续等待或取消确认。异步 buffer、参数对象和回调上下文的生命周期按公共头约定管理。不能用 `void *`、整数转换或额外 include 把原生句柄穿过公共边界。SirinLink 回调不得阻塞 HostComm；长命令在 HostCmd 执行。SPI/I2C 完成回调与 IMU 观察回调在其服务任务执行，仍必须有界且遵守对应接口的禁止阻塞约束；真正的 IRQ 上下文只调用明确标为 FromISR 的入口。

## 运行行为与资源

CAN1/2/3 各自保持 RX/TX worker，CAN4 收发共用一个 worker；HostComm/HostCmd、Indicators/SystemMonitor 各自独立。HostComm 每轮 RX 上限 1024 字节，原生 CAN RX worker 每批上限 32 帧；CAN4 在一轮内按最多 32 次收发或 1 ms 处理预算调度。原生 FDCAN ISR 排空硬件 FIFO，实际最长占用需板端负载验证。SPI、Serial、Indicators 按事件与最近截止点等待；批次边界的 Yield 只让出同优先级任务，不保证持续过载时低优先级任务得到 CPU。

SirinLink 活动请求表固定 10 项，覆盖排队、执行、完成及待发送结果。活动请求不因 2 秒缓存期限而过期；执行前表满返回 BUSY，同身份重试不重复执行。完成重放缓存仍为 4 项/2 秒。HostComm 固定持有一个待发送结果，发送成功或会话失效才取下一项。断连隔离旧结果，已经执行的业务不回滚。交付回调统一在 HostComm 释放保护后执行。

SPI/I2C 采用预留、保护外复制、发布；复制中的槽不能被派发或复用，取消按生命周期收敛。Serial 单次 Read/Write/Flush 的期限包含锁竞争；Transceive 将发送准备期限和发送完成后的响应期限分开。运行期日志池/队列耗尽只返回并计数，启动输出和 Fault 仍有独立合同。

静态 D1 状态与同步控制块、RTOS 堆、libc 堆、DMA 区和任务栈分别核算。Serial 每个通道槽预留两个静态二值信号量控制块，共 12 份（含 NONE 槽，最多初始化 10 个）；SirinLink 活动表和 pending 为固定容量。`SRNOS_SystemHeapBudget()` 是保守 RTOS 启动堆预算，不能把静态 D1 用量再次算入 heap；运行期通过 `SRNOS_KernelGetResources()` 读取堆和应用池副本，具体地址与预留以当前 ELF/map 为准。

## 命名例外与验证边界

仅保留有外部合同的原名：第三方原始源码/许可证、CubeMX 生成符号、FreeRTOS/CMSIS/HAL 所需入口和 USB/Microchip 适配 ABI（如 `HAL_*Callback`、`USBD_LL_*`、`DRV_SPI_TransferData`，按实际定义文件逐项检查）。历史来源清单及实测证据不改写。`SRNOS_` 是 SDK 线程名保留前缀，父项目线程名字不得使用它。

主机行为测试、独立公共头检查、父项目集成和 Debug/Release 构建用于检查源码与静态合同。执行成本、CPU、唤醒次数、临界区和 INS/CAN/传感器/CDC 组合负载应在目标固件验证；循环 hook ≤1 µs、统计增量 ≤1 个百分点、Monitor 平均 CPU ≤0.1%、监测临界区 ≤10 µs 是验收目标，不能由主机结果推断已达到。

## 单份持久化与两类变量迁移

将旧24C02接口和格式、`Param<T>`及access开关改为以下公开合同：使用`Persistent<T>(stable_id,name,default,min,max)`声明可写业务参数，应用配置非零parameter_application_id，类型/范围/默认值不可缺。只读运行量使用`Telemetry::Value<T>`并Publish带实际采样时间的缓存，不注册为参数；不要继续使用旧SirinLink原始value_ptr/getter源入口。C代码使用对应注册/值接口，不包含RTOS/HAL。

默认启用启动加载和业务SAVE/LOAD；原EEPROM内容不自动迁移或格式化。单份写入中断可丢失该记录，启动回默认或未校准降级，运行LOAD失败保留现值。PCB序列号20字节ASCII，专用工厂开关写入。SOURCE V1线编码保留，V2采用0x10–0x14以避开已有IMU/INS状态0x08/0x09。Storage/Telemetry和校准公共接口的使用示例及边界见相应模块README。

静态校准现通过 CONFIG 0x09 手动触发，0x0A 查询，保留加速度系数并下次启动应用；它不通过参数 WRITE/SAVE 伪装成普通业务变量。当前线上消息与兼容范围统一见 [SirinLink](SirinLink.md)，落盘布局与失效处理见 [Storage](Storage.md)。

## 静态 IPv4 接入

父项目在启动配置中同时开启 `ethernet.enabled` 和 `network.enabled`，声明静态地址及 UDP 端口。Network 启用后独占 Ethernet 原始收发；完整配置、限制与资源预算见 [Network](../Source/Services/Network/README.md)。

`SRNOS_NetworkConfig.udp_rx_select[4]` 默认为 NULL，表示每端口单槽满时丢新包。需要覆盖旧值或保留幂等停止事件时，在启动阶段为该端口注册纯选择器；锁存事件不因排队超时或断线消失，直到单消费者取走。`SRNOS_NetworkStatus` 提供替换/锁存计数。修改公共结构后整体重编译使用方；不得把会过期的速度命令标为 LATCH，不得在选择器中阻塞或操作控制状态。完整例子见 [Network](../Source/Services/Network/README.md#选择器与单消费者示例)。
