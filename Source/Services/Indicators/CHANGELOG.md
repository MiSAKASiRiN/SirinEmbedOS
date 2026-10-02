# Changelog

## Unreleased

- Low/1 KiB worker 按音符与心跳截止点等待，任务/ISR 请求通知唤醒，空闲永久阻塞；仅到期 LED DMA busy 时 1 tick 重试。主机截止点、唤醒、回绕回归通过；本轮未烧录。


- 2026-09-28：SRNOS_Indic 接入托管单轮与相对 1 ms 等待，灯效/蜂鸣器/心跳语义保留。 主机/构建证据见 `Docs/Validation/ManagedTasks-20260928/README.md`；本次未烧录，时序与开销仍待板测。

- 2026-09-28：完成 Support 职责整理；接口/时序/调度保持，固定硬件配置改为板级私有。21 组主机测试与固件集成构建通过；未板测。
