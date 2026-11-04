# Changelog

## Unreleased — VT02 SirinRMT 串口恢复

- 旧图传 InputIO 在 VT02 串口流重置时同时使显式注册的 SirinRMT 来源失效；保留键鼠与自定义数据的独立路由、接收任务和串口所有权。ASan/UBSan 主机集成覆盖溢出、重复包和恢复后的新数据，未进行板端测试。

## Unreleased — VT02/VT12 接入整理（2026-11-04）

- 明确 MCU 侧 RX/TX 方向、冻结配置和独占约束；新增完整用法指南与可编译公共示例，保留已验证的 wire 行为和 InputIO 资源。具体现场与硬件改动前的恢复材料保存在父项目。

## Unreleased - VT02显式TX（2026-11-03）

- 内置VT02来源提供Send/Flush，受既有Serial的tx_enabled限制；保持单一所有者和InputIO接收循环，不增加任务/自动发送。SDK主机集成和父项目Debug/C11/C++17公共头验证通过；当前VT02/VT12的0302/0309/0310三包30字节上行由NS核对成功，兼容性不泛化到其他固件或长度。

## Unreleased - VT02/VT12（2026-11-01）

- 按官方 2024 V1.6.4 增加 115200/8N1 的旧图传 A5 RX；0304 发布独立键鼠 Input，0302 原样送独立 VT02 Message transport。与裁判常规链路、VT03/13 互不混用。
- 新增 vt02_port、公有解码/绑定接口、串口格式/冲突检查与 InputIO 堆预算；新增静态缓冲，沿用原任务、优先级与栈，SDK/父项目需整体重编译。无旧图传 TX 或运动映射。
- ASan/UBSan 主机验证覆盖官方 CRC 表生成的固定向量、所有分片边界、粘包、符号极值、CRC/错误长度/未知命令/嵌套载荷、随机流、实际 InputIO/Input/Message 分发、按键边沿、独立时效、UART 异常/恢复、时钟回绕及有界轮询；父项目 Debug/Release 编译通过。未烧录，未进行真实图传或板端时序验证。


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
