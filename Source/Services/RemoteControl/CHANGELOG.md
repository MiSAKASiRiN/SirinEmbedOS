# Changelog

## Unreleased — W.Bus 丢帧处理

- 2026-10-21：修正单次接收机 frame_lost 导致 Input epoch 变化、父项目使能锁定的问题。仅 LOST 时跳过发布，不刷新任意有效字段或时间；FAILSAFE（含 LOST+FAILSAFE）立即失效，UART 溢出/错误/流重置仍撤权。独立解码器、公共 API、串口、任务和内存布局不变。
- SDK 实帧回归覆盖启动 LOST、有效帧后 LOST、同批混合、持续 LOST 过期、FAILSAFE 优先/同批恢复和串口异常；父项目回归验证短缺帧保持、有效字段 50 ms 边界/超过后停机、恢复不自动使能。板端结果由父项目验证记录维护。

## Unreleased — 输入与消息

- 2026-10-21：保留物理解码器并逐帧发布独立 Input 来源；添加 W.Bus 内置绑定及 Serial 丢帧恢复；删除 RC 全局仲裁、post_map_hook 与固定通道 API。修复 DR16 批次满帧后丢弃剩余字节的问题。

## 2026-10-20

- 新增独立 [W.BUS 接收模块](WBus/README.md)，通过公共 Serial 接口绑定/轮询，保留原始值和失控标志，不改动既有遥控仲裁。

- VT03 所有者支持可选 A5/遥控混合解析，`SRNOS_VT03Config`、Framer 及启动配置扩展，需整体重编译。Framer Init 清空回调；已识别 A5 的载荷不再被误识别为遥控帧，CRC16 失败整包丢弃，未完整包有 10 ms 超时。
- 原 RC 线程同时驱动 VideoLink 非阻塞发送；任务、优先级和栈预算不变。主机验证覆盖每个分片边界、载荷嵌入遥控帧、CRC 错误与恢复。

## Unreleased

- 2026-10-03：SRNOS_RC 纳入 UART P1，统一 High7（47）；执行周期、协议、栈和线程数不变。随 Serial 主机回归及父项目/Debug/Release 构建验证，板端时延待验。

- 2026-09-28：DR16、VT03 与仲裁接口统一使用 `SRNOS_*`，公开头位于 `Services/RemoteControl/`，不依赖 HAL/CMSIS/FreeRTOS。设备协议值和映射不变，Serial 通道由启动配置冻结，父项目只通过公开状态和仲裁 API 使用输入。旧头及名称已删除；迁移说明见 `Docs/SDKMigration.md`。验证范围为主机与构建；未重新烧录。

- 2026-09-28：SRNOS_RC 接入托管单轮与相对 1 ms 等待，解码与仲裁行为保留。验证范围为主机与构建；本次未烧录，时序与开销仍待板测。
