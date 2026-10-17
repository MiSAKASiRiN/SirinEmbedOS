# Changelog

## Unreleased

- 2026-09-29：增加 EEPROM 总览及 CONFIG 完整字段链接；示例代码未改。


- 2026-09-28：示例改用显式稳定 ID 的 Persistent PID 参数，配置 application_id/compatibility_version 并启用启动加载；新增只读 motor.encoder 遥测快照，说明 WRITE、SAVE、LOAD、dirty 和单份掉电回退合同。父项目只含 SDK 公共头。

- 2026-09-28：长期应用工作改为托管 step 描述，周期等待由 SDK 负责；观察 srnos_example_create_result 检查创建结果。 主机/构建证据见 `Docs/Validation/ManagedTasks-20260928/README.md`；本次未烧录，时序与开销仍待板测。

## 2026-10-17 文档整理

- 使用说明保留当前合同、入口和示例；日期战报与修改记录归入本文件。仅调整 Markdown，代码、原始测量数据和第三方源码未改动。

### README.md 中保留的历史说明

以下内容描述原文所述时期，不作为当前用法；当前入口见 [README.md](README.md)。

C 项目可使用 `SRNOS_ParametersRegisterPersistent` 和 `SRNOS_TelemetryRegister/Publish`。登记复制元数据，父项目在启动配置期完成注册，不在运行期新增。详细合同见 [Parameters](../../Source/Services/Parameters/README.md)、[Telemetry](../../Source/Services/Telemetry/README.md) 和 [Kernel](../../Source/Kernel/README.md)。本轮主机/构建验收不表示完成板端掉电或高频吞吐测试。
