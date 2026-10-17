# Changelog

## Unreleased

- 2026-10-03：补充四路默认 Classic 1 Mbit/s 与 CAN4 FD 显式仲裁/数据/BRS 配置片段，说明初始化冻结、adapter 限制、精确长度和 D1 容量，并链接完整 CANFD 示例。Minimal 应用源码仍只运行原心跳，不主动发送 CAN；P0/P1 策略不变，FD 物理通信/吞吐尚未板测。

- 2026-09-28：说明 Heartbeat 的创建位置、字段、返回值与持有句柄、启动入口返回后的生命周期，以及父项目查询/协作删除/参数存储与另一种启动声明方式。示例代码与固件不变。 文档中根 C++17 / Kernel C11 示例语法及本地链接检查通过。

- 2026-09-28：长期应用工作改为托管 step 描述，周期等待由 SDK 负责；观察 srnos_example_create_result 检查创建结果。 主机/构建证据见 `Docs/Validation/ManagedTasks-20260928/README.md`；本次未烧录，时序与开销仍待板测。

## 2026-10-17 文档整理

- 使用说明保留当前合同、入口和示例；日期战报与修改记录归入本文件。仅调整 Markdown，代码、原始测量数据和第三方源码未改动。

### README.md 中保留的历史说明

以下内容描述原文所述时期，不作为当前用法；当前入口见 [README.md](README.md)。

根 [接入示例](../../README.md#父项目接入) 说明 CMake 子模块接入；[板测记录](../../Docs/Validation/ManagedTasksBoard-20260928/README.md) 记录已执行的正常 Release 与专用测试固件验证。本示例应用源码保持心跳行为；新增 SDK FD 能力未重新烧录或板测。

## 静态 IPv4 接入（2026-10-14）
