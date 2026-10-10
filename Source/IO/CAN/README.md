# CAN 通路

Native/External 管理硬件收发，Router 管理 Classic 路由与 adapter，FD 管理独立的 FD 原始订阅与 one-shot 队列；三个 FDCAN handle、MCP 的 SPI/CS/INT 绑定与 GPIO EXTI 接线在 Board/Support/IO/can_board.cpp。硬件配置类型独立于 router context，MCP SPI 默认绑定共享同一配置。固定表之外无默认设备，显式 Attach 合同保持。

主机路由/绑定测试及父项目构建通过；CAN1/CAN3 的外接物理回环板测见 Tests/Board/CANThroughput，实际结果与范围以 Docs/Validation 为准。

## 启动模式与 FD 接口

四路控制器分别通过 `SRNOS_SystemConfig.can[channel - 1]` 在 `SRNOS_ApplicationConfigure()` 中声明。默认全部为 Classic、仲裁速率 1 Mbit/s、`data_bitrate=0`、`brs=false`。每路只选择 Classic 或 FD；启动校验后冻结，无运行期切换、重绑定或 HostLink 改写。`Controller.h` 提供配置校验和冻结配置副本查询，冻结前查询返回 false。最终配置与订阅/adapter 类型不匹配时拒绝启动；恢复只使用冻结配置。

| 配置 | 支持范围 |
|---|---|
| Classic 仲裁速率 | 125/250/500 kbit/s、1 Mbit/s；原始 Classic API 支持这些速率，现有电机等 adapter 仍仅允许 Classic 1 Mbit/s。 |
| FD 仲裁速率 | 125/250/500 kbit/s、1 Mbit/s。 |
| FD 开启 BRS | 应用显式填写数据速率 500 kbit/s、1/2/4/5/8 Mbit/s，且不得低于仲裁速率。 |
| FD 关闭 BRS | `brs=false` 且数据速率必须等于仲裁速率。 |

应用通过 `SirinEmbedOS/IO/CAN/FD/CANFD.h` 注册原始订阅、提交 one-shot 与读取统计，业务编码、分包和协议由应用定义。每路 FD 固定 32 个订阅、64 个待发槽，独立于 Classic adapter/group 调度。订阅/退订只在配置阶段进行；RX 回调在该路 RX worker、core 锁外执行，必须有界且不可阻塞，帧指针仅在回调期间有效。任务上下文的 `SRNOS_CANFDPostOneshot()` 复制帧，返回 OK 只表示软件接纳；优先级数值小者先发、同级 FIFO，TTL 为 0 时使用 20 ms，否则允许 1..INT32_MAX ms。

TTL 约束软件队列等待，在进入 backend 提交前检查；不取消已开始的 SPI/HAL 提交，也不约束硬件 FIFO、仲裁与重发延迟。CAN4 提交可能等待共享 SPI，TTL 不是线缆上的硬截止。`tx_complete_events` 在原生控制器按 TEF 完成帧计数，在 CAN4 按读回确认的 TX FIFO 事件计数；MCP 未启用 TEF，不能由该值推导逐帧交付量，纯 RX 共享中断不计入。

`SRNOS_CANFDFrame` 的 FDF 隐含为 1，长度必须精确取 0..8、12、16、20、24、32、48、64 B，不补齐非法长度或截断 payload；即使只有 0..8 B 仍是 FD。EXT 区分标准/扩展 ID；发送 BRS 必须等于冻结配置，ESI 仅供接收且禁止应用发送，FD 不支持 RTR。RX 保留实际 BRS/ESI，因此接收 BRS 可与本路发送策略不同。两种软件 RX 通路严格互斥，短 FD 不进入 Classic 回调，Classic/RTR 不进入 FD 回调。FD 收发正常计数，但跳过只容纳 8 B 的 Classic payload 调试日志。

原生 FDCAN 使用 PLL2Q 80 MHz。三路各占固定 `0x300` word Message RAM 窗口：Classic 实占 232 word，FD 实占 676 word；FD 每路 FIFO0 8 项、TX FIFO 28 项、TX event 8 项，payload 64 B，关闭 FIFO1 和专用 RX/TX buffer。CAN4 的 MCP 保持 40 MHz 与 SPI 10 MHz，Classic 为 32 RX＋32 TX、8 B（1024 B RAM），FD 为 16 RX＋12 TX、64 B（2016 B RAM）；TXQ/TEF/RX timestamp 关闭。CAN4 仅接受已冻结配置，并在启动配置与最终工作模式分别最多读取 20 次 OPMOD，确认成功后才发布 worker 句柄；细节见 [MCP](../../Devices/MCP2518FD/README.md) 和 [Board](../../../Board/README.md)。

离线测试覆盖配置互斥、精确 DLC、64 B、模式过滤、错误/超时及实际驱动和 worker 路径；CAN4 寄存器 fake 使用未修改的供应商驱动。2026-10-06 CAN1/3短接已验证80MHz、Classic1M和FD1/2/4/5/8M真实收发；Classic8B、FD8M64B各3k收发60秒及FD5M64B同档10秒通过。该次仅测CAN1/3，CAN4共享SPI结果见本文后续实测边界；CAN2、总线故障与四路/UART混合负载仍待验证，见 [实板记录](../../../Docs/Validation/CANFD-Board-20261006/README.md)。必须按长度、BRS、仲裁/数据速率计算总线占用：64 B、4 Mbit/s 的数据字段在合计 6000 帧/s 时已占 76.8%，尚未计仲裁、CRC、帧间隔、位填充和重传，不能视为达标配置。

## 托管线程

八个 RX/TX worker 改用统一框架及 SRNOS_CAN1..4_RX/TX 名称；RX/TX 保持分离。原生 RX 每轮最多处理 32 帧，满批次保留就绪事件并 Yield，空闲等待通知；不在自动清除的 Wait 后再次清除事件。外置 RX 保留 5 ms 硬件事件兜底检查，达到 32 帧批次上限主动续唤醒，并使用真正的 Yield。 栈、资源所有权和常驻线程数保留；当前优先级见下文。私有等待适配由 Kernel 驱动；普通等待不计入 step，处理期间 I/O/抢占/中断计入墙钟。统计不能替代板端时序验收。详见根文档及 Source/Kernel/README.md。

TX 入队与完成通知立即唤醒，无 adapter 和排队项时永久阻塞；仅有排队 one-shot 时按其最早过期点等待。CAN4 查询 FIFO 或提交帧发生 SPI 错误时，失败不一定产生后续 CAN 中断，因此为仍在排队的请求设置 1 ms 重试截止点；持续错误也受原 one-shot TTL（默认 20 ms）约束，队列清空后不再重试唤醒。真正的硬件 FIFO 满仍等待 TX 中断或 TTL，不增加原生 CAN 的固定重试。存在 adapter 的 tick/poll_tx 时仍保留 1 ms 工作节奏，因为原合同的 urgent poll 没有额外 ready 通知，不能只按周期发送间隔休眠。该 1 ms 是已注册业务的工作合同，空配置不再周期唤醒。

主机测试覆盖 TX 空闲/过期点/时钟回绕，以及真实原生 RX worker 的 70 帧突发分为 32/32/6、续唤醒和让出行为。CAN4 使用实际 TX backend 与 core，注入 SPI 查询/提交暂时失败且无后续 IRQ，核对重试成功、FIFO 满等待、持续错误 TTL 到期及空闲停止重试。线程栈和队列容量保留；负载与共享SPI已追加板测，见本文后续实测边界，总线故障恢复仍待验证。

父项目仅包含公开的 `Controller.h`、`Router/CAN.h`、`External/ExternalCAN.h` 或 `FD/CANFD.h`。worker、ISR 和冻结/启动装配声明位于私有头；GetStats(ch, out)/GetStats(dev, out) 在任务上下文返回调用者拥有的副本，禁止缓存旧版本内部统计指针。原生诊断与调试观测符号统一 SRNOS_ 前缀。

原生 RX/TX 通知的头文件声明必须是普通外部声明，只有 FDCAN.c 的默认定义使用 weak；router 提供强实现。若将 weak 放在共享声明上，C++ router 的实现也会继承弱属性，链接器可能选中空实现，造成硬件收帧而 RX 线程永远不醒。Tests/Core/check_foundation_elf.py 检查最终 RX/TX 通知均为强符号，不能仅凭单模块主机测试或硬件 TX 入队计数判断链路正常。

2026-09-28 后续已完成 CAN1/CAN3 外接短测：完整 INS 负载下合计 8000 帧/s 全部交付、CPU 95.21%，10000 提交/s 出现队列背压；不代表业务安全预算或 CAN2/CAN4 验收。见 [实测及修复](../../../Docs/Validation/CAN13-ADC-20260928/README.md)。

## 内存归属

Classic CAN Router 的 `s_ctx`（默认 59920 B，包含通道路由、adapter、one-shot 队列、group shadow、发送暂存与统计）通过 `SRNOS_ITCM_BSS` 放入 ITCM；SDK 在 ApplicationConfigure/总线注册之前清零。禁止全局构造中访问该专用状态。FD 的四路上下文固定放在普通 D1 BSS（默认合计 23312 B），每路 32 个订阅和 64 个 TX 槽即使配置 Classic 也预留，不从 heap 分配。Classic 容量、八个 worker 和栈保持不变；原生 RX 每轮预算仍为 32 帧，每次仅搬运 4 帧以留出 FD 回调栈空间。原生 RX ring 仍在 DTCM，SPI/DMA staging 仍在可达 SRAM，软件上下文不是 DMA 缓冲。

与 Watch 共用 ITCM 后当前默认 Release 尚余 2432 B，增大路由容量必须重新核对 map；超量会链接失败，不能静默回落到 D1。原生 RX 每次搬运 4 帧后，编译产物中回调前的静态调用链为 Debug 912 B / Release 896 B，较 32 帧局部数组节省 3016 B；这不包含应用回调、TLS 和异常上下文，2026-10-06六模式板测中CAN1/3 RX最小余量3092 B，只覆盖测试回调；父应用回调仍需自行验收。早期迁移证据见 [迁移与板测](../../../Docs/Validation/ITCM-20260929/README.md)；最新压力与栈证据见 [CAN1/3板测](../../../Docs/Validation/CANFD-Board-20261006/README.md)。

## 每路 3 kHz 收发的软件优化

目标为 CAN1–4 每路同时 3000 帧/s RX 与 3000 帧/s TX，四路共 24000 次收发/s。CAN1/3已按上述配置完成实测，四路同时运行仍待验收。Classic CAN 的收发共享物理总线，实际可用带宽还取决于 ID、DLC、位填充、仲裁及重传。adapter 周期单位仍为毫秒，3 kHz 指通道合计，不新增 333 µs 周期接口。

P0 为 CAN1/2/3 收发与 BMI/LSM/INS（Realtime / 48），P1 为 CAN4 收发及 UART 的 Serial/RC/Referee/RS485 worker（High7 / 47）。共享 SPI4 分发者保持 Realtime1（49），I2C4 保持 High6（46）。NVIC 的 FDCAN1–3、IMU EXTI9_5、SPI4 及其 DMA 为 5；MCP EXTI15_10、五路 UART IRQ 及十路 UART DMA IRQ 为 6。CAN4合并为一个4KiB服务线程；原生CAN任务、Classic队列容量和SPI时钟保持。CAN 回调必须有界、不可阻塞；永久高负载仍可能挤占较低优先级任务，不能仅凭优先级声明实时保证。

RX/TX 使用 FreeRTOS 直接任务通知 index 0，Kernel 生命周期控制使用 index 1；移除 ISR EventFlags 经 Timer daemon 中转。CAN4 单线程保存初始化前 pending 位，并接收 RX/TX/共享 INT；初始化成功才发布句柄，失败不留失效任务引用。通知位可合并，帧仍由 FIFO/ring 保存。原生 TX 每批完成只通知一次，但逐帧完成计数保持。

原生 RX ring 采用单生产者/单消费者 acquire/release 发布；满时丢弃新帧并计数，ISR 不再推进消费者索引。ISR 只处理入口看到的 FIFO 数量，持续到达由后续 IRQ 承接；worker 每轮仍最多 32 帧，每次搬运 4 帧。冻结路由表按 ID 二分后直接遍历，不逐帧复制订阅表或进临界区；冻结前保留快照路径。TX 无 adapter 时只处理 one-shot，group scratch 按实际使用初始化。十个自维护 CAN/SPI 热路径文件在 Release 使用 -O2，Debug 与供应商编译策略保持。

CAN4的状态/UA读取、RAM操作与UINC合并为每帧一个私有SPI序列，各物理事务之间仍允许IMU优先插入。错误恢复、诊断和回调统一由P1 `SRNOS_CAN4` 执行，应用回调必须有界且非阻塞，不持设备锁。

Bus-Off 由 HAL_FDCAN_ErrorStatusCallback 接入，ISR 仅记录并唤醒 TX worker。恢复状态机等待 100 ms，取消旧硬件 TX（最多等待 10 ms），Stop/Start 后最多等待 100 ms 确认恢复；最多三次失败后隔离至重启。隔离时不再提交硬件，但软件队列继续按 TTL 过期；旧硬件命令不重放。私有观测包含恢复状态、次数和失败计数。HAL 取消与实际总线重入仍待硬件验收。

本轮主机测试覆盖四路虚拟 1 秒各 3000 RX＋3000 TX、满环/连续 IRQ/通知交错、CAN4 查询和重试、Bus-Off 回绕与有界恢复，并验证电机反馈快照并发。虚拟时间测试不测 MCU 吞吐。构建、内存增量和上板验收清单见 [本轮记录](../../../Docs/Validation/CANRouter-20261002/README.md)。

当前 FD 接入及最新构建/内存证据见 [CAN FD验证](../../../Docs/Validation/CANFD-20261003/README.md)，80MHz与FD当前板测范围见 [CAN1/3实板记录](../../../Docs/Validation/CANFD-Board-20261006/README.md)。

## 优化前 CAN4 共享SPI实测基线（2026-10-06）

CAN1/CAN4短接：Classic单向3k完整交付，双向各2k整板CPU91.54%，请求各3k时生产者错过周期；FD5M/8M对齐配置64B各3k时MCP硬件RX溢出。默认8M仲裁采样点76.25%/80%不一致，测试将CAN1对齐80%后功能通过，但生产时序未改。双IMU/INS保持1kHz不代表CAN4吞吐达标；详见 [完整报告](../../../Docs/Validation/CAN4-SPI-20261006/README.md)。CAN2、四路/UART混合及物理故障恢复仍待验收。


## CAN4 单线程服务与预算

原生CAN1/2/3保留六个P0收发线程，CAN4使用一个P1/4KiB `SRNOS_CAN4`。正常按4RX/4TX交替，RX状态达到半满时按8RX/1TX交替；每帧检查通知。达到32帧或1ms后保留就绪状态并Yield，无固定延时；空闲原子等待通知，5ms兜底。TX完成后再次检查RX，避免只依赖一次边沿。SPI暂时失败/硬件满采用有界重试，仍保留软件TTL。

私有Classic/FD TX步进每次最多尝试一帧。Classic已poll出的item、周期游标、group合并和shadow保持到下一步；每周期one-shot优先预算16帧后允许adapter运行，避免持续one-shot使adapter饥饿。原生批量TX保持原行为。新状态位于普通D1 BSS，没有运行期分配或新增ITCM占用。

FD原生仲裁采样点80%，数据段≤4M为80%、5M为75%、8M为80%；TDC偏移继续由数据TSEG1与分频计算。Classic保持原时序；没有修改IOC或重新Generate。CAN4 FIFO容量、SPI4 10MHz、P0/P1及IMU采样/温控保持。

CAN4不再在每次唤醒额外读取CiINT。保留ABI中的CiINT派生调试字段（module_events、last_int_flags、rx_irq_no_rxif、tx_irq_no_txif）及core的tx_complete_events在该backend不累计；原生CAN仍保持原统计。CAN4应使用硬件引脚IRQ计数、rx_frames/tx_frames、真实FIFO溢出/错误与协议端到端确认观测，不能将tx_frames（提交或在途接纳）当作线缆交付。

## 2026-10-06 优化后验证

Classic 1M/8B双向各3k完成3×60s＋600s，零丢帧/拒绝/TTL过期/硬件RX溢出，78个十秒CPU窗口最高53.9632%。双IMU/INS保持1kHz，含6k过载的完成间隔/采集/SPI排队峰值1143/151/112µs；FD五速率320格式用例和过载恢复通过，64B各3k在5/8M下CPU约61%。实测仅CAN1/CAN4短接，CAN2/3/UART同时满载与物理故障尚未覆盖；原Flash恢复并释放探针。详见 [最终验证与原始证据](../../../Docs/Validation/CAN4-Optimization-20261006/README.md)。
