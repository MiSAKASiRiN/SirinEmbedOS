# Changelog

## Unreleased

- 2026-09-29：澄清关闭业务参数/主机存储不关闭启动 IMU 校准读取与应用；配置及代码不变。


- 2026-09-28：长期应用工作改为托管 step 描述，周期等待由 SDK 负责；观察 srnos_example_create_result 检查创建结果。 主机/构建证据见 `Docs/Validation/ManagedTasks-20260928/README.md`；本次未烧录，时序与开销仍待板测。

## 2026-10-17 文档整理

- 使用说明保留当前合同、入口和示例；日期战报与修改记录归入本文件。仅调整 Markdown，代码、原始测量数据和第三方源码未改动。

### README.md 中保留的历史说明

以下内容描述原文所述时期，不作为当前用法；当前入口见 [README.md](README.md)。

## 托管接口迁移

长期应用工作改为托管 step 描述，周期等待由 SDK 负责；观察 srnos_example_create_result 检查创建结果。 旧 ThreadConfig.entry 不再支持；公共创建/停止/删除及同步用法见 Source/Kernel/README.md。本次编译不意味着重新执行板端测试。
