# Changelog

## Unreleased

- 2026-09-28：全面迁为 SRNOS_Referee 命名和纯 SDK 类型，TX 超时明确为毫秒；删除空 TX Task，UI 结构保留 wire 尺寸。公共头/父项目构建验证，板端裁判链路待测。

- 2026-09-28：SRNOS_Referee 接入托管单轮与原相对延时；初始化失败返回统一出口，无独立发送线程。 主机/构建证据见 `Docs/Validation/ManagedTasks-20260928/README.md`；本次未烧录，时序与开销仍待板测。
