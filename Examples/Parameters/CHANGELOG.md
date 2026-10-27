# Changelog

## Unreleased

- 2026-09-29：增加 EEPROM 总览及 CONFIG 完整字段链接；示例代码未改。

- 2026-09-28：示例改用显式稳定 ID 的 Persistent PID 参数，配置 application_id/compatibility_version 并启用启动加载；新增只读 motor.encoder 遥测快照，说明 WRITE、SAVE、LOAD、dirty 和单份掉电回退合同。父项目只含 SDK 公共头。

- 2026-09-28：长期应用工作改为托管 step 描述，周期等待由 SDK 负责；观察 srnos_example_create_result 检查创建结果。验证范围为主机与构建；本次未烧录，时序与开销仍待板测。
