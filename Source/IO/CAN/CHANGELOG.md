# Changelog

## Unreleased — CAN 时间基准修复

- 2026-10-12：统一原生/外置 Classic/FD CAN 的回调、发送 TTL、适配器周期与重试到 `SRNOS_TimeMs()`，修复应用读取反馈年龄时混用 HAL 原点的问题；未绑定 backend 的截止点保持同域。公共函数签名和线程/内存布局不变。
- 增加 HAL 领先 12 ms 的回归、绑定前入队和回绕验证；Router/Native/External/FD/六种 Motors、公共头与 MSwerve Debug/Release 构建通过。移除父项目补丁后板上 13/13 在线，年龄正常；CAN4/FD/物理断线本轮未板测。记录：../../../Docs/Validation/CANClock-20261012/README.md。

## Unreleased

- 2026-10-06：CAN4合并为P1/4KiB单线程，4RX/4TX及半满8RX/1TX，32帧或1ms有界轮次；私有TX步进保留adapter游标/group/TTL，回调在该线程执行；CAN4停止逐唤醒读取CiINT，其旧派生调试字段与tx_complete_events保留但不累计，交付由端到端确认。FD原生采样点与MCP对齐，Classic时序不变。 Classic目标3×60s＋600s通过，最高十秒CPU53.9632%，IMU保护线通过；FD格式/负载/恢复、资源及原Flash恢复见 [最终记录](../../../Docs/Validation/CAN4-Optimization-20261006/README.md)。

- 2026-10-06：补充CAN1/CAN4 Classic/FD实测，3k双向尚未达标：Classic生产者错过周期，FD64B出现MCP RX FIFO溢出；8M默认时序首帧失败、测试专用80%仲裁采样点对照通过。生产实现未改，证据见 [报告](../../../Docs/Validation/CAN4-SPI-20261006/README.md)。

- 2026-10-06：CAN1/3短接验证六模式和全部合法DLC/标准扩展ID，共356用例通过。Classic8B/FD8M64B各3k收发60秒分别CPU32.00%/35.13%，FD5M64B同档10秒通过；4M及以下64B同档过载，拒绝/TTL完全解释未交付。RX栈余最小3092 B、双IMU/INS连续1kHz；原Flash恢复，CAN2/4和物理故障恢复未测。见 [完整记录](../../../Docs/Validation/CANFD-Board-20261006/README.md)。

- 2026-10-03：四路独立启动冻结 Classic/FD 配置，默认 Classic 1 Mbit/s；新增 FD 原始订阅/one-shot/统计 API，精确支持 0..8/12/16/20/24/32/48/64 B，发送 BRS 固定、接收保留实际 EXT/BRS/ESI，禁止 ESI 发送和跨模式订阅/接收。现有 adapter 仍限定 Classic 1 Mbit/s；业务 FD 协议由父应用定义。
- 2026-10-03：FD 每路 32 订阅/64 TX 槽固定在 D1，四路默认 23312 B；Classic ITCM 队列不变。原生 80 MHz、每路固定 0x300 word Message RAM 窗口，FD 实占 676 word；MCP FD 16 RX＋12 TX/64 B 占 2016 B，Classic 配置保留。新增模式/速率/长度/错误主机回归与实际供应商寄存器 fake；PCB 离线，所有 FD 位时序/TDC、吞吐和混合负载待测。
- 2026-10-03：原生 RX 每轮仍最多 32 帧，改为每次搬运 4 帧，避免 4 KiB worker 栈被 64 B 帧批次挤满；不扩栈/heap。CAN4 拒绝冻结前初始化，配置和最终 OPMOD 有界确认，初始化失败不发布句柄；FD 最长 TTL 的毫秒到 tick 换算使用 64 位，保留永久等待。CAN4 完成事件只在确认 TXIF 后计数，纯 RX 共享 IRQ 不计；该计数不是逐帧交付量。针对性主机回归通过，硬件未测。

- 2026-10-03：按最新 P0/P1 要求：CAN1/2/3 保持 Realtime（48），CAN4 调至 High7（47）；IMU/INS 同属 P0，UART 服务同属 P1。MCP EXTI 改为 NVIC 6；通知、容量和收发状态机不变。

- 2026-10-03：按每路 3 kHz RX＋3 kHz TX 目标提高 CAN worker/IRQ 优先级；直接任务通知、冻结路由直读、惰性 group 暂存、批量 TX 唤醒、CAN4 重复 SPI 查询消减及 Release 热路径 -O2。原生满环改丢新 SPSC，真实 HAL Bus-Off 回调接入有界取消/退避/重入/隔离。新增真实驱动/worker 和四路虚拟负载主机回归；公共 API、八线程、栈/容量保持。离线结果及未验硬件范围见 [记录](../../../Docs/Validation/CANRouter-20261002/README.md)。

- 2026-09-29：59920 B Router 软件上下文改用 SRNOS_ITCM_BSS，启动配置前清零；原生 RX ring、DMA staging、队列和八个 worker 保持不变。路由/worker 主机回归、父项目与 Debug/Release 通过，板端预填后软件复位清零通过；当前 CAN1/CAN3 未短接，外部收发压力待验。见 [记录](../../../Docs/Validation/ITCM-20260929/README.md)。

- 2026-09-28：INS 有效后完成 CAN1↔CAN3 1 Mbit/s、8 B 阶梯板测，累计交付 298342 帧；8000 帧/s 档 CPU 95.21%，10000 提交/s 档拒绝 15658 次，已接纳帧无损交付。恢复正常 Release；CAN2/CAN4 和故障恢复待测。详见 [记录](../../../Docs/Validation/CAN13-ADC-20260928/README.md)。

- 2026-09-28：CAN1/CAN3 外接回环发现 RX 硬件收到 2000 帧而 router 为 0。移除私有通知声明上的 weak，仅保留默认定义的 weak，确保 RX/TX router 强实现被链接；新增最终 ELF 检查，旧 ELF 明确失败、修复构建通过。未改变 HAL/IOC、线程优先级、容量或线格式。

- 修复 CAN4 事件等待中的临时 SPI 拒绝重试：查询/提交错误为未发送 one-shot 设置 1 ms 截止点，无需依赖可能不会出现的 TX IRQ；持续错误不延长原 TTL，空闲仍永久等待，实际 FIFO 满及 native 空位通知语义保留。实际 external backend/core 的无 IRQ 查询/提交失败、持续错误和回绕主机回归通过；板端待测。

- 2026-09-28：原生 CAN worker 私有文件改为 `native_can_rx.cpp`、`native_can_tx.cpp` 和 `native_can_backend_internal.h`，同步显式装配和主机测试；core/实际 RX worker 回归通过，调度和线协议不变。

- CAN 公共头移除启动/worker/ISR/冻结入口；GetStats 改为任务上下文输出副本，板端 ManagedTasks 测试同步迁移。FDCAN/RS485/Indicators 自维护观测全局统一 SRNOS_ 命名。

- 保留八个 RX/TX worker；原生 RX 有界 32 帧并通知续跑、取消重复 clear，外置 drain 上限后真正 Yield。TX 空配置永久等待，one-shot 按过期点等待，adapter tick/urgent poll 保留 1 ms。主机 core 与真实 RX worker 回归通过，CAN 负载板测待验证。


- 2026-09-28：八个 RX/TX worker 改用统一框架及 SRNOS_CAN1..4_RX/TX 名称；原通知等待和排空批次保留。 主机/构建证据见 `Docs/Validation/ManagedTasks-20260928/README.md`；本次未烧录，时序与开销仍待板测。

- 2026-09-28：板级配置与 router 私有状态解耦；Native 通过私有板级头取得 handle，MCP 默认绑定统一。路由语义不变，主机及固件构建验证通过，未板测。

## 2026-10-17 文档整理

- README 仅说明当前接口、使用方式和边界，补充接入示例；历史结果移至本文件，保留证据链接。此项不修改实现，不新增板端验证。

### 从 README 归档的历史记录

主机路由/绑定测试及父项目构建通过；CAN1/CAN3 的外接物理回环板测见 Tests/Board/CANThroughput，实际结果与范围以 Docs/Validation 为准。

2026-10-12 在 MSwerve 父项目发现并修复 HAL 与 RTOS 约 12 ms 原点差导致的反馈年龄回绕；主机覆盖启动/TTL/重试/超时/回绕，原生 Classic 13 个电机板测通过。CAN4/FD 本轮仅主机验证。见 [验证记录](../../../Docs/Validation/CANClock-20261012/README.md)。

离线测试覆盖配置互斥、精确 DLC、64 B、模式过滤、错误/超时及实际驱动和 worker 路径；CAN4 寄存器 fake 使用未修改的供应商驱动。2026-10-06 CAN1/3短接已验证80MHz、Classic1M和FD1/2/4/5/8M真实收发；Classic8B、FD8M64B各3k收发60秒及FD5M64B同档10秒通过。该次仅测CAN1/3，CAN4共享SPI结果见本文后续实测边界；CAN2、总线故障与四路/UART混合负载仍待验证，见 [实板记录](../../../Docs/Validation/CANFD-Board-20261006/README.md)。必须按长度、BRS、仲裁/数据速率计算总线占用：64 B、4 Mbit/s 的数据字段在合计 6000 帧/s 时已占 76.8%，尚未计仲裁、CRC、帧间隔、位填充和重传，不能视为达标配置。

八个 RX/TX worker 改用统一框架及 SRNOS_CAN1..4_RX/TX 名称；RX/TX 保持分离。原生 RX 每轮最多处理 32 帧，满批次保留就绪事件并 Yield，空闲等待通知；不在自动清除的 Wait 后再次清除事件。外置 RX 保留 5 ms 硬件事件兜底检查，达到 32 帧批次上限主动续唤醒，并使用真正的 Yield。 栈、资源所有权和常驻线程数保留；当前优先级见下文。私有等待适配由 Kernel 驱动；普通等待不计入 step，处理期间 I/O/抢占/中断计入墙钟。统计不能替代板端时序验收。详见根文档及 Source/Kernel/README.md。

主机测试覆盖 TX 空闲/过期点/时钟回绕，以及真实原生 RX worker 的 70 帧突发分为 32/32/6、续唤醒和让出行为。CAN4 使用实际 TX backend 与 core，注入 SPI 查询/提交暂时失败且无后续 IRQ，核对重试成功、FIFO 满等待、持续错误 TTL 到期及空闲停止重试。线程栈和队列容量保留；负载与共享SPI已追加板测，见本文后续实测边界，总线故障恢复仍待验证。

2026-09-28 后续已完成 CAN1/CAN3 外接短测：完整 INS 负载下合计 8000 帧/s 全部交付、CPU 95.21%，10000 提交/s 出现队列背压；不代表业务安全预算或 CAN2/CAN4 验收。见 [实测及修复](../../../Docs/Validation/CAN13-ADC-20260928/README.md)。

与 Watch 共用 ITCM 后当前默认 Release 尚余 2432 B，增大路由容量必须重新核对 map；超量会链接失败，不能静默回落到 D1。原生 RX 每次搬运 4 帧后，编译产物中回调前的静态调用链为 Debug 912 B / Release 896 B，较 32 帧局部数组节省 3016 B；这不包含应用回调、TLS 和异常上下文，2026-10-06六模式板测中CAN1/3 RX最小余量3092 B，只覆盖测试回调；父应用回调仍需自行验收。早期迁移证据见 [迁移与板测](../../../Docs/Validation/ITCM-20260929/README.md)；最新压力与栈证据见 [CAN1/3板测](../../../Docs/Validation/CANFD-Board-20261006/README.md)。

目标为 CAN1–4 每路同时 3000 帧/s RX 与 3000 帧/s TX，四路共 24000 次收发/s。CAN1/3已按上述配置完成实测，四路同时运行仍待验收。Classic CAN 的收发共享物理总线，实际可用带宽还取决于 ID、DLC、位填充、仲裁及重传。adapter 周期单位仍为毫秒，3 kHz 指通道合计，不新增 333 µs 周期接口。

本轮主机测试覆盖四路虚拟 1 秒各 3000 RX＋3000 TX、满环/连续 IRQ/通知交错、CAN4 查询和重试、Bus-Off 回绕与有界恢复，并验证电机反馈快照并发。虚拟时间测试不测 MCU 吞吐。构建、内存增量和上板验收清单见 [本轮记录](../../../Docs/Validation/CANRouter-20261002/README.md)。

当前 FD 接入及最新构建/内存证据见 [CAN FD验证](../../../Docs/Validation/CANFD-20261003/README.md)，80MHz与FD当前板测范围见 [CAN1/3实板记录](../../../Docs/Validation/CANFD-Board-20261006/README.md)。

## 优化前 CAN4 共享SPI实测基线（2026-10-06）

CAN1/CAN4短接：Classic单向3k完整交付，双向各2k整板CPU91.54%，请求各3k时生产者错过周期；FD5M/8M对齐配置64B各3k时MCP硬件RX溢出。默认8M仲裁采样点76.25%/80%不一致，测试将CAN1对齐80%后功能通过，但生产时序未改。双IMU/INS保持1kHz不代表CAN4吞吐达标；详见 [完整报告](../../../Docs/Validation/CAN4-SPI-20261006/README.md)。CAN2、四路/UART混合及物理故障恢复仍待验收。

## 2026-10-06 优化后验证

Classic 1M/8B双向各3k完成3×60s＋600s，零丢帧/拒绝/TTL过期/硬件RX溢出，78个十秒CPU窗口最高53.9632%。双IMU/INS保持1kHz，含6k过载的完成间隔/采集/SPI排队峰值1143/151/112µs；FD五速率320格式用例和过载恢复通过，64B各3k在5/8M下CPU约61%。实测仅CAN1/CAN4短接，CAN2/3/UART同时满载与物理故障尚未覆盖；原Flash恢复并释放探针。详见 [最终验证与原始证据](../../../Docs/Validation/CAN4-Optimization-20261006/README.md)。
