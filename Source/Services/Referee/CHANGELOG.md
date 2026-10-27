# Changelog

## 2026-10-25

- 删除从未参与发送的旧 TX 状态及仅清零这些状态的 `SRNOS_RefereeTxInit()`。旧应用移除该调用；SDK 按启动配置绑定 Serial，现有发送 API、协议和线程保持不变。

## Unreleased

- 2026-10-03：SRNOS_Referee 纳入 UART P1，统一 High7（47）；执行周期、协议、栈和线程数不变。随 Serial 主机回归及父项目/Debug/Release 构建验证，板端时延待验。

- 2026-09-28：全面迁为 SRNOS_Referee 命名和纯 SDK 类型，TX 超时明确为毫秒；删除空 TX Task，UI 结构保留 wire 尺寸。公共头/父项目构建验证，板端裁判链路待测。

- 2026-09-28：SRNOS_Referee 接入托管单轮与原相对延时；初始化失败返回统一出口，无独立发送线程。验证范围为主机与构建；本次未烧录，时序与开销仍待板测。
