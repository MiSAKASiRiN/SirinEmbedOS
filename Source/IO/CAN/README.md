# CAN 通路

Native/External 管理硬件收发，Router 管理 Classic 路由与 adapter，FD 管理独立的 FD 原始订阅与 one-shot 队列；三个 FDCAN handle、MCP 的 SPI/CS/INT 绑定与 GPIO EXTI 接线在 Board/Support/IO/can_board.cpp。硬件配置类型独立于 router context，MCP SPI 默认绑定共享同一配置。固定表之外无默认设备，显式 Attach 合同保持。

## 毫秒时间基准

CAN1–4 的 Classic/FD 路由、adapter `on_rx/poll_tx/tick`、发送 TTL 和重试使用公开 `SRNOS_TimeMs()`（FreeRTOS 1 ms tick）。调度前为 0，32 位自然回绕；backend 绑定前后保持同一原点。`on_rx` 时间是软件分发时间，不是帧在线缆上的到达时间。SDK 电机 `last_rx_ms` 可直接与应用 `SRNOS_TimeMs()` 比较；应用自己调用解析或 tick 时也须使用此时间域。HAL/TIM7、DWT `TimeUs` 和独立诊断/日志时间不保证同原点，不能混减，也不能用固定启动偏移补偿。

## 启动模式与 FD 接口

四路控制器分别通过 `SRNOS_SystemConfig.can[channel - 1]` 在 `SRNOS_ApplicationConfigure()` 中声明。默认全部为 Classic、仲裁速率 1 Mbit/s、`data_bitrate=0`、`brs=false`。每路只选择 Classic 或 FD；启动校验后冻结，无运行期切换、重绑定或 HostLink 改写。`Controller.h` 提供配置校验和冻结配置副本查询，冻结前查询返回 false。最终配置与订阅/adapter 类型不匹配时拒绝启动；恢复只使用冻结配置。

| 配置 | 支持范围 |
|---|---|
| Classic 仲裁速率 | 125/250/500 kbit/s、1 Mbit/s；原始 Classic API 支持这些速率，现有电机等 adapter 仍仅允许 Classic 1 Mbit/s。 |
| FD 仲裁速率 | 125/250/500 kbit/s、1 Mbit/s。 |
| FD 开启 BRS | 应用显式填写数据速率 500 kbit/s、1/2/4/5/8 Mbit/s，且不得低于仲裁速率。 |
| FD 关闭 BRS | `brs=false` 且数据速率必须等于仲裁速率。 |

应用通过 `SirinEmbedOS/IO/CAN/FD/CANFD.h` 注册原始订阅、提交 one-shot 与读取统计，业务编码、分包和协议由应用定义。每路 FD 固定 32 个订阅、64 个待发槽，独立于 Classic adapter/group 调度。订阅/退订只在配置阶段进行；RX 回调在对应原生 RX worker 或 CAN4 统一服务线程、core 锁外执行，必须有界且不可阻塞，帧指针仅在回调期间有效。任务上下文的 `SRNOS_CANFDPostOneshot()` 复制帧，返回 OK 只表示软件接纳；优先级数值小者先发、同级 FIFO，TTL 为 0 时使用 20 ms，否则允许 1..INT32_MAX ms。

TTL 约束软件队列等待，在进入 backend 提交前检查；不取消已开始的 SPI/HAL 提交，也不约束硬件 FIFO、仲裁与重发延迟。CAN4 提交可能等待共享 SPI，TTL 不是线缆上的硬截止。`tx_complete_events` 在原生控制器按 TEF 完成帧计数；CAN4 未启用 TEF，当前 backend 不累计该字段，不能用它推导逐帧交付量。

`SRNOS_CANFDFrame` 的 FDF 隐含为 1，长度必须精确取 0..8、12、16、20、24、32、48、64 B，不补齐非法长度或截断 payload；即使只有 0..8 B 仍是 FD。EXT 区分标准/扩展 ID；发送 BRS 必须等于冻结配置，ESI 仅供接收且禁止应用发送，FD 不支持 RTR。RX 保留实际 BRS/ESI，因此接收 BRS 可与本路发送策略不同。两种软件 RX 通路严格互斥，短 FD 不进入 Classic 回调，Classic/RTR 不进入 FD 回调。FD 收发正常计数，但跳过只容纳 8 B 的 Classic payload 调试日志。

原生 FDCAN 使用 PLL2Q 80 MHz。三路各占固定 `0x300` word Message RAM 窗口：Classic 实占 232 word，FD 实占 676 word；FD 每路 FIFO0 8 项、TX FIFO 28 项、TX event 8 项，payload 64 B，关闭 FIFO1 和专用 RX/TX buffer。CAN4 的 MCP 保持 40 MHz 与 SPI 10 MHz，Classic 为 32 RX＋32 TX、8 B（1024 B RAM），FD 为 16 RX＋12 TX、64 B（2016 B RAM）；TXQ/TEF/RX timestamp 关闭。CAN4 仅接受已冻结配置，并在启动配置与最终工作模式分别最多读取 20 次 OPMOD，确认成功后才发布 worker 句柄；细节见 [MCP](../../Devices/MCP2518FD/README.md) 和 [Board](../../../Board/README.md)。

## 托管线程

| 任务 | 数量 | 每任务栈 | 优先级 |
|---|---:|---:|---:|
| `SRNOS_CAN1_RX/TX`、`SRNOS_CAN2_RX/TX`、`SRNOS_CAN3_RX/TX` | 6 | 4096 B | 48 |
| `SRNOS_CAN4`（收发共用） | 1 | 4096 B | 47 |

原生 RX 每轮最多 32 帧，每次取 4 帧；满批次保留就绪事件并 Yield，空闲等待通知。CAN4 的收发批次和 5 ms 兜底见下文。等待位于周期统计之外，处理期间的 I/O、抢占和中断计入墙钟。应用回调与 TLS 还会消耗任务栈，栈容量不能代替实测水位。

TX 入队与完成通知立即唤醒，无 adapter 和排队项时永久阻塞；仅有排队 one-shot 时按其最早过期点等待。CAN4 查询 FIFO 或提交帧发生 SPI 错误时，失败不一定产生后续 CAN 中断，因此为仍在排队的请求设置 1 ms 重试截止点；持续错误也受原 one-shot TTL（默认 20 ms）约束，队列清空后不再重试唤醒。真正的硬件 FIFO 满仍等待 TX 中断或 TTL，不增加原生 CAN 的固定重试。存在 adapter 的 tick/poll_tx 时仍保留 1 ms 工作节奏，因为原合同的 urgent poll 没有额外 ready 通知，不能只按周期发送间隔休眠。该 1 ms 是已注册业务的工作合同，空配置不再周期唤醒。

父项目仅包含公开的 `Controller.h`、`Router/CAN.h`、`External/ExternalCAN.h` 或 `FD/CANFD.h`。worker、ISR 和冻结/启动装配声明位于私有头；GetStats(ch, out)/GetStats(dev, out) 在任务上下文返回调用者拥有的副本，禁止缓存旧版本内部统计指针。原生诊断与调试观测符号统一 SRNOS_ 前缀。

原生 RX/TX 通知的头文件声明必须是普通外部声明，只有 FDCAN.c 的默认定义使用 weak；router 提供强实现。若将 weak 放在共享声明上，C++ router 的实现也会继承弱属性，链接器可能选中空实现，造成硬件收帧而 RX 线程永远不醒。Tests/Core/check_foundation_elf.py 检查最终 RX/TX 通知均为强符号，不能仅凭单模块主机测试或硬件 TX 入队计数判断链路正常。

## 内存归属

Classic 每路固定预留 64 个 adapter 槽和 64 个 RX 路由槽；原始订阅与 adapter 共用 RX 槽。注册只占用已有槽位，不按协议数追加 heap；设备对象、应用缓存及扩大编译期容量另占 RAM。应检查 Attach/Subscribe 返回值，不能把固定上限当成所有注册一定成功。

Classic CAN Router 的 `s_ctx`（默认 59920 B，包含通道路由、adapter、one-shot 队列、group shadow、发送暂存与统计）通过 `SRNOS_ITCM_BSS` 放入 ITCM；SDK 在 ApplicationConfigure/总线注册之前清零。禁止全局构造中访问该专用状态。FD 的四路上下文固定放在普通 D1 BSS（默认合计 23312 B），每路 32 个订阅和 64 个 TX 槽即使配置 Classic 也预留，不从 heap 分配。Classic 使用七个 worker（见上表），原生 RX 每轮预算为 32 帧，每次仅搬运 4 帧以留出 FD 回调栈空间。原生 RX ring 仍在 DTCM，SPI/DMA staging 仍在可达 SRAM，软件上下文不是 DMA 缓冲。

## 调度与恢复

P0 为 CAN1/2/3 收发与 BMI/LSM/INS（Realtime / 48），P1 为 CAN4 收发及 UART 的 Serial/RC/Referee/RS485 worker（High7 / 47）。共享 SPI4 分发者保持 Realtime1（49），I2C4 保持 High6（46）。NVIC 的 FDCAN1–3、IMU EXTI9_5、SPI4 及其 DMA 为 5；MCP EXTI15_10、五路 UART IRQ 及十路 UART DMA IRQ 为 6。CAN4 使用一个 4 KiB 服务线程。CAN 回调必须有界、不可阻塞；永久高负载仍可能挤占较低优先级任务，不能仅凭优先级声明实时保证。

RX/TX 使用 FreeRTOS 直接任务通知 index 0，Kernel 生命周期控制使用 index 1；ISR 直接唤醒对应 worker，无 Timer daemon 中转。CAN4 单线程保存初始化前 pending 位，并接收 RX/TX/共享 INT；初始化成功才发布句柄，失败不留失效任务引用。通知位可合并，帧仍由 FIFO/ring 保存。原生 TX 每批完成只通知一次，但逐帧完成计数保持。

原生 RX ring 采用单生产者/单消费者 acquire/release 发布；满时丢弃新帧并计数，ISR 不推进消费者索引。ISR 只处理入口看到的 FIFO 数量，持续到达由后续 IRQ 承接；worker 每轮仍最多 32 帧，每次搬运 4 帧。冻结路由表按 ID 二分后直接遍历，不逐帧复制订阅表或进临界区；冻结前保留快照路径。TX 无 adapter 时只处理 one-shot，group scratch 按实际使用初始化。自维护 CAN/SPI 热路径在 Release 使用 -O2，具体文件由 SDK CMake 配置。

CAN4的状态/UA读取、RAM操作与UINC合并为每帧一个私有SPI序列，各物理事务之间仍允许IMU优先插入。错误恢复、诊断和回调统一由P1 `SRNOS_CAN4` 执行，应用回调必须有界且非阻塞，不持设备锁。

Bus-Off 由 HAL_FDCAN_ErrorStatusCallback 接入，ISR 仅记录并唤醒 TX worker。恢复状态机等待 100 ms，取消旧硬件 TX（最多等待 10 ms），Stop/Start 后最多等待 100 ms 确认恢复；最多三次失败后隔离至重启。隔离时不再提交硬件，但软件队列继续按 TTL 过期；旧硬件命令不重放。私有观测包含恢复状态、次数和失败计数。HAL 取消与实际总线重入仍待硬件验收。


## CAN4 单线程服务与预算

原生CAN1/2/3保留六个P0收发线程，CAN4使用一个P1/4KiB `SRNOS_CAN4`。正常按4RX/4TX交替，RX状态达到半满时按8RX/1TX交替；每帧检查通知。达到32帧或1ms后保留就绪状态并Yield，无固定延时；空闲原子等待通知，5ms兜底。TX完成后再次检查RX，避免只依赖一次边沿。SPI暂时失败/硬件满采用有界重试，仍保留软件TTL。

私有Classic/FD TX步进每次最多尝试一帧。Classic已poll出的item、周期游标、group合并和shadow保持到下一步；每周期one-shot优先预算16帧后允许adapter运行，避免持续one-shot使adapter饥饿。原生批量TX保持原行为。步进状态位于普通 D1 BSS，不进行运行期分配。

FD原生仲裁采样点80%，数据段≤4M为80%、5M为75%、8M为80%；TDC偏移继续由数据TSEG1与分频计算。Classic 使用冻结的仲裁时序，CAN4 FIFO 容量见前述硬件配置，SPI4 为 10 MHz。

CAN4 不在每次唤醒额外读取 CiINT。保留ABI中的CiINT派生调试字段（module_events、last_int_flags、rx_irq_no_rxif、tx_irq_no_txif）及core的tx_complete_events在该backend不累计；原生CAN仍保持原统计。CAN4应使用硬件引脚IRQ计数、rx_frames/tx_frames、真实FIFO溢出/错误与协议端到端确认观测，不能将tx_frames（提交或在途接纳）当作线缆交付。

## Classic 原始接收示例

在 `ApplicationConfigure` 中注册，随后配置冻结。下面是 C 接入片段；回调运行于高优先级 CAN worker，示例不在回调里打印、等待或触发控制输出。

```c
#include "SirinEmbedOS/IO/CAN/Router/CAN.h"
static void receive_status(SRNOS_CANChannel channel,
                           const SRNOS_CANFrame *frame, void *user) {
    (void)channel; (void)user;
    if (frame->dlc != 8) return;
    // 在这里做有界解码或复制；frame 指针仅在本次回调内有效。
}
int register_status_receiver(void) {
    const SRNOS_CANRxFilter filter = {0x321, 0}; // 标准数据帧
    return SRNOS_CANRouterSubscribeRaw(SRNOS_CAN_CH_NTFDCAN1,
                                      filter, receive_status, 0);
}
```

Classic 任务侧一次性发送使用 `SRNOS_CANRouterPostOneshot()`，返回成功只表示复制入队；发送完成和外部 ACK 需要相应观测。电机 adapter 示例见 [Motors](../../Devices/Motors/README.md)，FD 完整接入见 [CANFD 示例](../../../Examples/CANFD/README.md)。业务使用同一 SDK 毫秒时钟，按新鲜度判断反馈。

吞吐评估须按实际 ID、长度、BRS、仲裁/数据速率、填充和重传计算占用；通道合计 3 kHz 不表示支持 333 µs adapter 周期。四路并发能力不能由单路测试推导。增加容量后重新检查 map，ITCM 超量会链接失败，不会静默回落到 D1。

## 验证入口

在 SDK 根执行：

```sh
bash Tests/IO/CAN/Router/run_host_tests.sh
bash Tests/IO/CAN/FD/run_host_tests.sh
bash Tests/IO/CAN/Native/run_host_tests.sh
bash Tests/IO/CAN/External/run_host_tests.sh
```

主机模型覆盖路由、队列、TTL、模式和恢复状态机，不能测 MCU 实际吞吐或物理 Bus-Off 恢复。各轮构建、板测及限制集中在 [CHANGELOG](CHANGELOG.md)，验证方法见 [验证指南](../../../Docs/Validation.md)。
