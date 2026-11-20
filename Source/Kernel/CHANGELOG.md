# Changelog

## Unreleased — 2026-11-19

- MP257 复用线程、同步、对象池、CMSIS-RTOS2 和 heap_5。新目标使用 Cortex-M33 port、SysTick 和 DDR 布局。已通过 remoteproc 运行周期任务。


## Unreleased — 2026-11-11 配置与资源合同

- 串口角色列表仅负责占用冲突；协议纯校验同时用于系统预检和实际绑定，修正 W.Bus/UART7 模式不匹配未被预检拒绝的问题。UART 日志后端未选择时不再传入 log_port 通道。
- 启动预算改为汇总各模块资源定义旁的计算，固定 60 KiB 启动余量、现有 heap 容量及线程栈/优先级保持；Logging 按实际池/队列载荷补足预算，FD 按冻结通道数计入发送存储。保留注册前预检与冻结前终检，并说明 Input/Message 注册状态会影响查询结果。
- 仅安排本轮固件与公共头编译检查；未新增或运行主机测试，未烧录，调度、协议与堆峰值仍需对应板端验证。

## Unreleased - VT02/VT12（2026-11-01）

- 按官方 2024 V1.6.4 增加 115200/8N1 的旧图传 A5 RX；0304 发布独立键鼠 Input，0302 原样送独立 VT02 Message transport。与裁判常规链路、VT03/13 互不混用。
- 新增 vt02_port、公有解码/绑定接口、串口格式/冲突检查与 InputIO 堆预算；新增静态缓冲，沿用原任务、优先级与栈，SDK/父项目需整体重编译。无旧图传 TX 或运动映射。
- ASan/UBSan 主机验证覆盖官方 CRC 表生成的固定向量、所有分片边界、粘包、符号极值、CRC/错误长度/未知命令/嵌套载荷、随机流、实际 InputIO/Input/Message 分发、按键边沿、独立时效、UART 异常/恢复、时钟回绕及有界轮询；父项目 Debug/Release 编译通过。未烧录，未进行真实图传或板端时序验证。


## Unreleased — Blackbox worker

- 2026-10-24：SystemConfig 增加默认关闭的 Blackbox，启动配置冻结前 Prepare、heap 初始化后分配队列/创建独占 NOR worker；低优先级 8、4096 B 栈，不阻塞应用就绪屏障。
- 保守预算计入队列、服务状态和 RTOS 余量；系统配置主机测试链接真实预算实现。板端调度与栈水位仍需实测，未改变其他任务优先级。

## Unreleased — 启动标识

- 2026-10-21：新增只读 SRNOS_SystemBootId()；ApplicationConfigure 前调用板级有界 RNG 初始化，标识独立于 NTP 和调度时钟，不是认证凭据。公共头/结构检查及父项目 Release 构建通过；EngNaviTest 实板运行与复位后标识变化通过。

## 2026-10-21 SirinLink 命名

- 启动配置和内部服务入口统一使用 sirin_link_* / SRNOS_SirinLink*；任务名称 HostComm 与线程/启动顺序保持。公共头、Kernel 回归及 12 个集成固件构建通过，未烧录。

## Unreleased — 输入与消息

- 2026-10-21：新增 messages/遥控来源启动配置校验和运行存储预算；bind_channels 前移到 HostLinkPrepare 之前，Input/Message 冻结后、osKernelInitialize 后一次分配资源。线程优先级保持，RC worker 改为 InputIO。

## 2026-10-20

- 配置验证增加 VideoLink 对 VT03 端口与 RX/TX 全双工的要求；默认关闭。未绑定及 RX-only 配置拒绝、有效 UART9 配置通过的主机回归已覆盖。线程布局与启动时序保持，字段注册需早于 HostLink 目录冻结。

## 历史验证补充

以下仅对应原测试固件与负载，当前使用方式见 [README](README.md)。

新增独立父项目板测，覆盖 400 次协作创建/删除、TLS、清理次序、同步删除保护和资源耗尽恢复。

公共隔离后的新固件已复测四个完整窗口，每次 400 个生命周期、2846 项检查零失败。两组统计 CPU 差值约 0.99 个百分点，完整 hook 最大 0.68 µs；这些是该测试固件的短时结果，未覆盖长期负载。

主机测试使用 pthread 假内核运行实际封装，覆盖生命周期、同步、回绕与统计；父项目接入和固件链接由构建检查覆盖。这些主机/构建结果不代替板测。已完成本板 Release 负载下的 hook/CPU/临界区、生命周期、INS 与 CDC 短测；外部 CAN 负载、长时间压力和 Debug hook 时序仍待验收。

## Unreleased — 静态 IPv4

- 2026-10-14：固定 lwIP 2.2.1 / IPv4+ARP+UDP+ICMP，默认 IP 192.168.1.50/24；网络、可选网关和 UDP 端口由上层启动配置冻结。Network 单线程持有协议栈和二层收发，不改变 MCO/RMII/DMA/IRQ。新增 6 KiB 栈、7 KiB heap 预算及约 22 KiB 静态 D1；明确排队期限、link epoch 和原始接口所有权。主机/构建及父项目板测记录见 Network 文档。

## Unreleased — Ethernet 二层接入

- 2026-10-13：新增 Ethernet 冻结配置、LAN8720A 链路管理、原始帧 API 与低优先级 worker；ETH IRQ 7、D2 DMA 新增 9216 B，现有时钟/MPU/生成区不变。默认关闭，父项目显式开启；详见 Source/IO/Ethernet 文档与父工程实测记录。

## Unreleased

- 2026-10-10：HostComm 栈增加 1 KiB，HostLink 保守启动预算 8→9 KiB；线程数量、优先级及 heap 分区不变。 Debug 构建及板端启动/保存通过。

- 2026-10-06：CAN4收发合并为一个SRNOS_CAN4，CAN线程数8→7、CAN4栈预算8→4KiB；保守基础启动heap预算64→60KiB，P0/P1与IMU线程不变。联合验证结果与未验边界见 [CAN4 板测摘要](../IO/CAN/CHANGELOG.md#can4-20261006)。

- 2026-10-03：SystemConfig.can[4] 新增每控制器启动期 Classic/FD 互斥配置，默认 Classic 1 Mbit/s；应用显式声明 FD 仲裁/数据/BRS，内核初始化前完成兼容校验、复制与冻结。复用既有八个 CAN worker 与 P0/P1 优先级、线程栈及 heap；FD 固定 32 订阅/64 TX 槽每路，四路上下文默认 23312 B 在 D1 BSS。配置冻结/资源准备与硬件就绪分开，CAN4 成功初始化才发布通知句柄；硬件离线待测。

- 2026-10-03：P0 为 CAN1/2/3 收发与 BMI/LSM/INS（Realtime / 48），P1 为 CAN4 收发及 UART 的 Serial/RC/Referee/RS485 worker（High7 / 47）。共享 SPI4 分发者保持 Realtime1（49），I2C4 保持 High6（46）。父应用1..39、周期、栈及任务数量不变，板端混合负载仍待验。

- 2026-10-03：CAN 八线程统一 Realtime（48），SPI4 Realtime1（49），Serial 分发 High5（45），IMU/INS 及 I2C4 原值保持。无新增任务、无栈/heap 扩容，父应用优先级合同不变；CAN index 0 直接通知与生命周期 index 1 分离。验证范围为主机与构建，组合负载未板测。

- AppInit接入有界身份/校准/参数加载，默认开启持久化与HostComm SAVE/LOAD，工厂身份写默认关闭；应用参数归属由稳定ID/版本声明。INS温控持续、校准就绪独立门控，无新增生产线程或优先级变更；板端启动及存储压力待测。

- 2026-09-28：公共隔离后固件复测四次 400 生命周期/2846 检查均零失败；统计新增约 0.99 个百分点，需保留余量优化。板测适配独立编译，应用无原生依赖。

- 2026-09-28：Kernel 的 RTOS/CMSIS、Board 与 Source include 都是 SDK 私有编译依赖。生产应用只链接 Platform 并包含 SDK 公共头；需要原生 RTOS 的验证代码单独编译为测试适配目标。接口独立 C11/C++17、依赖闭包和父项目真实编译命令由构建检查覆盖。验证范围为主机与构建；未重新烧录。

- 2026-09-28：完善父项目管理指南：启动列表不返回句柄/不保证晚于 ApplicationMain，运行期创建与回收示例、DelayUntil 时钟域、init 未完成状态、受限批次事件处理示例。 文档中根 C++17 / Kernel C11 示例语法及本地链接检查通过。

- 2026-09-28：新增独立父项目真实 RTOS 板测，覆盖生命周期、TLS、同步删除保护、资源耗尽及循环统计成本对照。

- 2026-09-28：新增托管任务/时间/同步接口、启动预留池与协作删除，统一 SDK 任务创建与命名，并接入循环统计。验证范围为主机与构建；本次未烧录，时序与开销仍待板测。

- 2026-09-28：SystemMonitor替代Diagnostics线程，1 Hz ADC/任务统计与HostLink发布；增加ADC板级BDMA接入、逐槽统计和主机工具，默认取消扫栈。Indicators/生成文件不变。验证范围为主机与构建，未烧录/板测。

- 指示器改经 Services/Indicators 私有头接入；线程配置与初始化顺序保持，板级 heap/入口移入 Support/System。验证范围为主机与构建；未板测。

- I2C4 改为四槽分发及每槽完成信号量，保守启动同步预算增加 512 B；未增加生产线程或改变线程栈、优先级与 heap 分区。
