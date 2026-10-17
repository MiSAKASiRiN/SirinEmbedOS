# Changelog

## 2026-10-17 文档整理

- README 按当前使用方式、示例和约束组织；不在使用文档追加版本战报。

## Unreleased

- 2026-10-03：SRNOS_Referee 纳入 UART P1，统一 High7（47）；执行周期、协议、栈和线程数不变。随 Serial 主机回归及父项目/Debug/Release 构建验证，板端时延待验。

- 2026-09-28：全面迁为 SRNOS_Referee 命名和纯 SDK 类型，TX 超时明确为毫秒；删除空 TX Task，UI 结构保留 wire 尺寸。公共头/父项目构建验证，板端裁判链路待测。

- 2026-09-28：SRNOS_Referee 接入托管单轮与原相对延时；初始化失败返回统一出口，无独立发送线程。 主机/构建证据见 `Docs/Validation/ManagedTasks-20260928/README.md`；本次未烧录，时序与开销仍待板测。
