# CAN 通路

Native/External/Router 管理收发、队列和路由；三个 FDCAN handle、MCP 的 SPI/CS/INT 绑定与 GPIO EXTI 接线在 Board/Support/IO/SRNOS_CANBoard。硬件配置类型独立于 router context，MCP SPI 默认绑定共享同一配置。固定表之外无默认设备，显式 Attach 合同保持。

主机路由/绑定测试及父项目构建通过；CAN1/CAN3 的外接物理回环板测见 Tests/Board/CANThroughput，实际结果与范围以 Docs/Validation 为准。

## 托管线程

八个 RX/TX worker 改用统一框架及 SRNOS_CAN1..4_RX/TX 名称；RX/TX 保持分离。原生 RX 每轮最多处理 32 帧，满批次保留就绪事件并 Yield，空闲等待通知；不在自动清除的 Wait 后再次清除事件。外置 RX 保留 5 ms 硬件事件兜底检查，达到 32 帧批次上限主动续唤醒，并使用真正的 Yield。 栈、优先级、资源所有权和常驻线程数保留。私有等待适配由 Kernel 驱动；普通等待不计入 step，处理期间 I/O/抢占/中断计入墙钟。统计不能替代板端时序验收。详见根文档及 Source/Kernel/README.md。

TX 入队与完成通知立即唤醒，无 adapter 和排队项时永久阻塞；仅有排队 one-shot 时按其最早过期点等待。CAN4 查询 FIFO 或提交帧发生 SPI 错误时，失败不一定产生后续 CAN 中断，因此为仍在排队的请求设置 1 ms 重试截止点；持续错误也受原 one-shot TTL（默认 20 ms）约束，队列清空后不再重试唤醒。真正的硬件 FIFO 满仍等待 TX 中断或 TTL，不增加原生 CAN 的固定重试。存在 adapter 的 tick/poll_tx 时仍保留 1 ms 工作节奏，因为原合同的 urgent poll 没有额外 ready 通知，不能只按周期发送间隔休眠。该 1 ms 是已注册业务的工作合同，空配置不再周期唤醒。

主机测试覆盖 TX 空闲/过期点/时钟回绕，以及真实原生 RX worker 的 70 帧突发分为 32/32/6、续唤醒和让出行为。CAN4 使用实际 TX backend 与 core，注入 SPI 查询/提交暂时失败且无后续 IRQ，核对重试成功、FIFO 满等待、持续错误 TTL 到期及空闲停止重试。线程优先级、栈和队列容量保留；尚未重新板测 CAN 满负载、总线故障和共享 SPI 延迟。

父项目仅包含 CAN.h/ExternalCAN.h。worker、ISR 和冻结/启动装配声明位于私有头；GetStats(ch, out)/GetStats(dev, out) 在任务上下文返回调用者拥有的副本，禁止缓存旧版本内部统计指针。原生诊断与调试观测符号统一 SRNOS_ 前缀。

原生 RX/TX 通知的头文件声明必须是普通外部声明，只有 FDCAN.c 的默认定义使用 weak；router 提供强实现。若将 weak 放在共享声明上，C++ router 的实现也会继承弱属性，链接器可能选中空实现，造成硬件收帧而 RX 线程永远不醒。Tests/Core/check_foundation_elf.py 检查最终 RX/TX 通知均为强符号，不能仅凭单模块主机测试或硬件 TX 入队计数判断链路正常。

2026-09-28 后续已完成 CAN1/CAN3 外接短测：完整 INS 负载下合计 8000 帧/s 全部交付、CPU 95.21%，10000 提交/s 出现队列背压；不代表业务安全预算或 CAN2/CAN4 验收。见 [实测及修复](../../../Docs/Validation/CAN13-ADC-20260928/README.md)。

## 内存归属

CAN Router 的 `s_ctx`（默认 59920 B，包含通道路由、adapter、one-shot 队列、group shadow、发送暂存与统计）通过 `SRNOS_ITCM_BSS` 放入 ITCM；SDK 在 ApplicationConfigure/总线注册之前清零。禁止全局构造中访问该专用状态。队列容量、八个 worker、优先级、栈和调度语义保持不变。原生 RX ring 仍在 DTCM，SPI/DMA staging 仍在可达 SRAM，软件上下文不是 DMA 缓冲。

与 Watch 共用 ITCM 后默认 Release 尚余 2528 B，增大路由容量必须重新核对 map；超量会链接失败，不能静默回落到 D1。见 [迁移与板测](../../../Docs/Validation/ITCM-20260929/README.md)。本次板子 CAN1/CAN3 未短接，未重做外部收发压力测试。
