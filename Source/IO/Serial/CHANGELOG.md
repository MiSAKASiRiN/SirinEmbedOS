# Changelog

## Unreleased — 2026-11-11 配置与资源合同

- 模块提供按端口数计算的私有启动预算；worker 创建与预算共用 2048 B 栈常量，每端口 2560 B 预留增加队列/互斥锁/分配余量的编译期容量约束。容量、DMA、优先级与运行行为保持。
- 本轮验证限定为编译，未新增或运行主机测试，未进行 UART/RS485 板端验证。

## Unreleased

- 2026-11-03：增加只读 `SRNOS_SerialGetPortStats`，供应用复用既有诊断传输读取内置协议端口统计；不新增缓冲、任务或串口所有权，不改变 RX/TX 行为。父项目Debug及C11/C++17公共头通过，CDC1板端连续读取验证通过；本轮UART9业务数据未收到，实际通信待验证。

- 2026-10-03：Serial、RC、Referee、RS485 统一 UART P1（High7 / 47）；UART IRQ 与十路 DMA IRQ 统一 NVIC 6。协议、容量、栈和线程数不变，父应用 RAW 消费线程不自动提级。

- 2026-10-03：公共 Serial 分发者从 AboveNormal 提至 High5（45），与 IMU/INS 同级、低于 CAN（48）；协议线程、栈、队列和 DMA 所有权保持。Serial 主机回归通过，CAN/UART 实际组合负载待板测。

- Serial 公共头直接导入 SDK 等待常量，文档明确 `SRNOS_NO_WAIT` / `SRNOS_WAIT_FOREVER` 与有限 INT32_MAX 毫秒范围；零等待和无限等待唤醒主机回归通过，保持原硬件 watchdog。

- Serial 使用固定 RX/TX 状态信号量和截止时间等待，移除常驻 1 ms 轮询；公共总超时包括取锁，失败恢复间隔至少 100 ms。主机新增锁竞争、入睡竞争、回绕及空闲等待验证通过；静态信号量内存计入 map，UART/RS485 板测待验证。

- 2026-09-28：SRNOS_Serial 接入托管循环；保留启动、单轮处理和通知/1 tick 等待。验证范围为主机与构建；本次未烧录，时序与开销仍待板测。

- UART 板级接入移入 Board/Support/IO/serial_board.c；回调、DMA 与 RS485 行为不变。验证范围为主机与构建；未板测。

## 历史验证补充

本轮主机回归覆盖总超时、锁竞争后的剩余时间、等待前 RX 到达、32 位时钟回绕、空闲永久等待及已有事务/协议场景。尚未重新进行 UART/RS485 实板时序与负载验证。
