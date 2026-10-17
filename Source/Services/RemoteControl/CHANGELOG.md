# Changelog

## 2026-10-17 文档整理

- README 按当前使用方式、示例和约束组织；不在使用文档追加版本战报。

## Unreleased

- 2026-10-03：SRNOS_RC 纳入 UART P1，统一 High7（47）；执行周期、协议、栈和线程数不变。随 Serial 主机回归及父项目/Debug/Release 构建验证，板端时延待验。

- 2026-09-28：DR16、VT03 与仲裁接口统一使用 `SRNOS_*`，公开头位于 `Services/RemoteControl/`，不依赖 HAL/CMSIS/FreeRTOS。设备协议值和映射不变，Serial 通道由启动配置冻结，父项目只通过公开状态和仲裁 API 使用输入。旧头及名称已删除；迁移说明见 `Docs/SDKMigration.md`。 本轮主机/构建范围见 `Docs/Validation/SRNOSBoundary-20260928/README.md`；未重新烧录。

- 2026-09-28：SRNOS_RC 接入托管单轮与相对 1 ms 等待，解码与仲裁行为保留。 主机/构建证据见 `Docs/Validation/ManagedTasks-20260928/README.md`；本次未烧录，时序与开销仍待板测。
