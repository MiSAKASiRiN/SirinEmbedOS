# P0/P1 优先级调整（2026-10-03）

用户将目标明确为 P0：CAN1/2/3 与 IMU，P1：CAN4/UART。本轮在已有 CAN 优化上调整任务、共享 SPI 仲裁和 IRQ；之前的 [CAN 优化记录](../CANRouter-20261002/README.md) 保留当时证据，当前策略以本文为准。未追加提交、未烧录或操作 PCB。

## 当前分级

P0/P1 为功能等级，不是直接使用 NVIC 0/1。CMSIS 任务数值越大越优先，NVIC 数值越小越优先；所有涉及 FromISR 的 IRQ 保持在允许的 5..15。

| 等级 / 依赖 | 线程 | CMSIS 优先级 | 对应 IRQ |
|---|---|---|---|
| P0 | CAN1/2/3 RX/TX、BMI、LSM、INS | Realtime / 48 | FDCAN1–3 IT0/IT1、IMU EXTI9_5 为 5 |
| P1 | CAN4 RX/TX、Serial、RC、Referee、RS485 | High7 / 47 | MCP EXTI15_10、五个 UART、十路 UART DMA 为 6 |
| 共享 SPI4 依赖 | SPI4 dispatcher | Realtime1 / 49 | SPI4、DMA2 Stream0/1 为 5 |
| 其他 | I2C4 dispatcher | High6 / 46，保持 | 保持原配置 |

IMU 包括原始采样和 INS 融合/温控处理链。BMI/LSM 仍由周期任务发起采样，经 SPI 完成通知/回调交付；LSM EXTI 已配置但未用于唤醒采样，本轮没有增加 DRDY 驱动。UART 包括 SDK 的协议处理 worker；父项目自行创建的 RAW 消费线程仍按应用优先级合同配置。

SPI4 保持高于两个等级，以完成它们共享的总线事务；在事务选择上优先 IMU，最多连续四笔后让出给一笔等待的低层请求。低层内优先 CAN4，最多四笔后给普通请求一笔。每类 FIFO，活动 DMA 不抢占；八槽、排队/活动期限、取消及缓冲区所有权均保持。

公共 API、任务数量、栈、采样/融合周期、总线速率、帧格式、队列与堆容量、DMA 银行不变。P0 持续过载仍可能压缩 P1 的 CPU 时间，公平仲裁只约束事务选择，不能代替整板时延验收。

## 验证

- SPI 实际分发器主机回归：IMU 优先于更早排队的 CAN；三类饱和 100 笔为 80 IMU、16 CAN4、4 普通；无 IMU 的 25 笔为 20 CAN4、5 普通。原 deadline/取消/恢复/IRQ 回归通过。
- `python3 Tests/run_host_tests.py`：28 套通过，包括 SPI、Serial、CAN core/native/external、IMU/INS。主机替身不模拟真实 RTOS 抢占时延。
- `python3 Tests/Build/check_sdk.py`：十个父项目、91 公共头/158 编译及依赖单元、第三方哈希、13 条应用 include 隔离、C++/TLS、容量覆盖及越界拒绝通过。
- 独立目录 `build/can-priority-20261003/Debug` 与 `Release` 编译通过，ELF 内存/TLS/向量/CAN 强回调门禁通过。结构与 Debug 对象命名检查通过；先前已记录的 Release 命名标准库/TLS 误报本轮未修改检查器或重新宣称通过。
- 26 项 CAN/IMU/SPI/UART IRQ 在 IOC 与生成代码一致，逐一核对 UART 的十路 DMA 请求和 handler 接线；任务清单包含表外单独建立的 LSM。
- `python3 Board/generate.py` 实际生成成功、I2C 时序检查通过；相对本轮快照仅 gpio.c/dma.c 的优先级变化，415 个 USER CODE 区域逐字保持。Support 仅同步 UART/DMA 同优先级的旧注释，cursor 临界保护保留。快照：`build/cubemx/20261003-002754-yuxutyfz`。

RAM 占用与上一轮一致：Release D1 254432 B、D2 6848 B、D3 544 B、ITCM 63104 B（余 2432 B）；Flash 为 211968 B。Debug D1 254400 B、Flash 283488 B，其余 RAM 相同。构建摘要与 ELF SHA256 随本文保存，完整编译日志位于独立构建目录。

PCB 离线，本轮不能证明四路各 3 kHz RX＋3 kHz TX、IMU 连续性或 UART 协议期限已经达标。上板需同时观察 P0 采样/CAN 完整性、P1 背压/延迟、SPI 排队超时及 CPU 余量。

后续已增加CAN FD与80MHz时钟，并进一步降低原生RX栈搬运占用；本文保留优先级轮当时的构建与资源证据，当前CAN配置和尺寸见 [CAN FD记录](../CANFD-20261003/README.md)。
