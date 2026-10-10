# Changelog

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
