# 指示器服务

内部服务通过 `indicators_internal.h` 供 Kernel 调用；父项目通过 SRNOS_Buzzer* / SRNOS_WS2812* 业务接口使用设备，初始化与 worker 接口为 SDK 私有。`SRNOS_IndicatorsPrepare` 在 HAL 完成、配置冻结后、调度前只调用一次，经板级初始化取得蜂鸣器结果。bringup 关闭时仍初始化设备、清灯并服务蜂鸣器队列；开启时第一灯提交黄色一次，依次播放 1000/1500/2000 Hz 各 80 ms。初始化/输出失败计数而不阻止启动；DWT 等待保留有界自旋。

Indicators worker 仍为 Low/1024 B，按蜂鸣器下一音符截止点与 500 ms 心跳截止点等待；没有待办工作时永久阻塞。新音符/停止请求通知唤醒，不占父项目任务通知。心跳每 500 ms 翻转一次，DMA busy 时等待下次循环，延迟唤醒不补发。`SRNOS_IndicatorsState` 仍供调试器观测；硬件初始化及 DMA1 Stream0/1 入口位于 Board/Support/Devices。服务不重新领取硬件或新增线程。

主机测试覆盖提示关闭、重复 prepare、失败、busy 和延迟调度；编译和计数不证明实际声光正常。

## 托管线程

SRNOS_Indic 接入托管单轮及事件/截止时间等待，灯效/蜂鸣器/心跳语义保留。只有心跳已到期但 LED DMA 仍 busy 时使用 1 tick 重试。 栈、优先级、资源所有权和常驻线程数保留。私有等待适配由 Kernel 驱动；普通等待不计入 step，处理期间 I/O/抢占/中断计入墙钟。统计不能替代板端时序验收。详见根文档及 Source/Kernel/README.md。

主机测试新增空闲无限等待、音符期限、500 ms 心跳、ISR/任务唤醒和时钟回绕。实板声光节奏与 CPU 降幅待验证，本轮未烧录。
