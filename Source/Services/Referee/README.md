# Referee

本模块的公共接口位于 `Include/SirinEmbedOS/Services/Referee/`：`referee.h` 提供协议与状态，`UI/` 提供编码，`Transmitter/RefereeTransmitter.h` 提供发送。全部自维护声明采用 `SRNOS_Referee*` / `SRNOS_REFEREE_*`；UI 和线协议结构大小保持原合同。父项目不包含 RTOS/HAL 头，不直接操作 UART。

发送走 Serial 复制队列，Write/WriteStr/Transmit 的 timeout 为毫秒，支持 `SRNOS_NO_WAIT/FOREVER`；期限包含锁竞争与队列提交，返回成功表示已复制接纳，不表示 UART 线发送完成。只有任务上下文可调用阻塞发送，调用者 buffer 在成功返回后可复用。原空 TX Task 入口已删除；接收健康使用 `SRNOS_RefereeReceiverReady()`，SDK 自行完成固定初始化及 worker 装配。

UI 布局属于应用，SDK 仅编码已有协议。C 公共头可独立按 C11/C++17 编译；结构断言与父项目链接见构建回归，实际裁判链路与 UI 显示仍需板测。

## 托管线程

SRNOS_Referee 接入托管单轮与原相对延时；初始化失败返回统一出口，无独立发送线程。 栈、优先级、资源所有权和常驻线程数保留。私有等待适配由 Kernel 驱动；普通等待不计入 step，处理期间 I/O/抢占/中断计入墙钟。统计不能替代板端时序验收。详见根文档及 Source/Kernel/README.md。
