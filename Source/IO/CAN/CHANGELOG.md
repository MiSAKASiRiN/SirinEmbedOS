# Changelog

## Unreleased

- 2026-09-29：59920 B Router 软件上下文改用 SRNOS_ITCM_BSS，启动配置前清零；原生 RX ring、DMA staging、队列和八个 worker 保持不变。路由/worker 主机回归、父项目与 Debug/Release 通过，板端预填后软件复位清零通过；当前 CAN1/CAN3 未短接，外部收发压力待验。见 [记录](../../../Docs/Validation/ITCM-20260929/README.md)。

- 2026-09-28：INS 有效后完成 CAN1↔CAN3 1 Mbit/s、8 B 阶梯板测，累计交付 298342 帧；8000 帧/s 档 CPU 95.21%，10000 提交/s 档拒绝 15658 次，已接纳帧无损交付。恢复正常 Release；CAN2/CAN4 和故障恢复待测。详见 [记录](../../../Docs/Validation/CAN13-ADC-20260928/README.md)。

- 2026-09-28：CAN1/CAN3 外接回环发现 RX 硬件收到 2000 帧而 router 为 0。移除私有通知声明上的 weak，仅保留默认定义的 weak，确保 RX/TX router 强实现被链接；新增最终 ELF 检查，旧 ELF 明确失败、修复构建通过。未改变 HAL/IOC、线程优先级、容量或线格式。

- 修复 CAN4 事件等待中的临时 SPI 拒绝重试：查询/提交错误为未发送 one-shot 设置 1 ms 截止点，无需依赖可能不会出现的 TX IRQ；持续错误不延长原 TTL，空闲仍永久等待，实际 FIFO 满及 native 空位通知语义保留。实际 external backend/core 的无 IRQ 查询/提交失败、持续错误和回绕主机回归通过；板端待测。

- 2026-09-28：原生 CAN worker 私有文件改为 `native_can_rx.cpp`、`native_can_tx.cpp` 和 `native_can_backend_internal.h`，同步显式装配和主机测试；core/实际 RX worker 回归通过，调度和线协议不变。

- CAN 公共头移除启动/worker/ISR/冻结入口；GetStats 改为任务上下文输出副本，板端 ManagedTasks 测试同步迁移。FDCAN/RS485/Indicators 自维护观测全局统一 SRNOS_ 命名。

- 保留八个 RX/TX worker；原生 RX 有界 32 帧并通知续跑、取消重复 clear，外置 drain 上限后真正 Yield。TX 空配置永久等待，one-shot 按过期点等待，adapter tick/urgent poll 保留 1 ms。主机 core 与真实 RX worker 回归通过，CAN 负载板测待验证。


- 2026-09-28：八个 RX/TX worker 改用统一框架及 SRNOS_CAN1..4_RX/TX 名称；原通知等待和排空批次保留。 主机/构建证据见 `Docs/Validation/ManagedTasks-20260928/README.md`；本次未烧录，时序与开销仍待板测。

- 2026-09-28：板级配置与 router 私有状态解耦；Native 通过私有板级头取得 handle，MCP 默认绑定统一。路由语义不变，主机及固件构建验证通过，未板测。
