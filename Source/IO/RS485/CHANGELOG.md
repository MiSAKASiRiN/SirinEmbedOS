# Changelog

## Unreleased

- 2026-10-03：SRNOS_RS485 纳入 UART P1，统一 High7（47）；执行周期、协议、栈和线程数不变。随 Serial 主机回归及父项目/Debug/Release 构建验证，板端时延待验。

- 2026-09-28：SRNOS_RS485 接入托管循环，队列等待移到统计外，事务应答/静默等待仍计入墙钟，原批处理保留。 主机/构建证据见 `Docs/Validation/ManagedTasks-20260928/README.md`；本次未烧录，时序与开销仍待板测。
