# Changelog

## Unreleased

- 2026-10-03：Serial、RC、Referee、RS485 统一 UART P1（High7 / 47）；UART IRQ 与十路 DMA IRQ 统一 NVIC 6。协议、容量、栈和线程数不变，父应用 RAW 消费线程不自动提级。

- 2026-10-03：公共 Serial 分发者从 AboveNormal 提至 High5（45），与 IMU/INS 同级、低于 CAN（48）；协议线程、栈、队列和 DMA 所有权保持。Serial 主机回归通过，CAN/UART 实际组合负载待板测。

- Serial 公共头直接导入 SDK 等待常量，文档明确 `SRNOS_NO_WAIT` / `SRNOS_WAIT_FOREVER` 与有限 INT32_MAX 毫秒范围；零等待和无限等待唤醒主机回归通过，保持原硬件 watchdog。

- Serial 使用固定 RX/TX 状态信号量和截止时间等待，移除常驻 1 ms 轮询；公共总超时包括取锁，失败恢复间隔至少 100 ms。主机新增锁竞争、入睡竞争、回绕及空闲等待验证通过；静态信号量内存计入 map，UART/RS485 板测待验证。


- 2026-09-28：SRNOS_Serial 接入托管循环；保留启动、单轮处理和通知/1 tick 等待。 主机/构建证据见 `Docs/Validation/ManagedTasks-20260928/README.md`；本次未烧录，时序与开销仍待板测。

- UART 板级接入移入 Board/Support/IO/serial_board.c；回调、DMA 与 RS485 行为不变。 本次主机及构建验证见 Docs/Validation/BoardSupport-20260928/README.md；未板测。

## 2026-10-17 文档整理

- README 仅说明当前接口、使用方式和边界，补充接入示例；历史结果移至本文件，保留证据链接。此项不修改实现，不新增板端验证。

### 从 README 归档的历史记录

本轮主机回归覆盖总超时、锁竞争后的剩余时间、等待前 RX 到达、32 位时钟回绕、空闲永久等待及已有事务/协议场景。尚未重新进行 UART/RS485 实板时序与负载验证。
