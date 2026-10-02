# Changelog

## Unreleased

- 2026-09-28：说明 Heartbeat 的创建位置、字段、返回值与持有句柄、启动入口返回后的生命周期，以及父项目查询/协作删除/参数存储与另一种启动声明方式。示例代码与固件不变。 文档中根 C++17 / Kernel C11 示例语法及本地链接检查通过。

- 2026-09-28：长期应用工作改为托管 step 描述，周期等待由 SDK 负责；观察 srnos_example_create_result 检查创建结果。 主机/构建证据见 `Docs/Validation/ManagedTasks-20260928/README.md`；本次未烧录，时序与开销仍待板测。
