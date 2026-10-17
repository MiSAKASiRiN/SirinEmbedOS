# Changelog

## Unreleased

- 2026-09-29：修正文档：当前例程只关闭业务参数加载/主机存储命令，启动仍读取身份/校准；原始数据不套用校准，未改示例配置。


- 2026-09-28：长期应用工作改为托管 step 描述，周期等待由 SDK 负责；观察 srnos_example_create_result 检查创建结果。 主机/构建证据见 `Docs/Validation/ManagedTasks-20260928/README.md`；本次未烧录，时序与开销仍待板测。

## 2026-10-17 文档整理

- 使用说明保留当前合同、入口和示例；日期战报与修改记录归入本文件。仅调整 Markdown，代码、原始测量数据和第三方源码未改动。

### README.md 中保留的历史说明

以下内容描述原文所述时期，不作为当前用法；当前入口见 [README.md](README.md)。

2026-09-27：四颗器件均 READY，LSM ID=0x6A。最终遥测固件已完成两个 10 秒正常窗口、取消/DTR 重连和软件复位短测；未完成十分钟验收。实测数据与精确 ELF SHA 见 [验证记录](../../Docs/Validation.md)。

## 托管接口迁移

长期应用工作改为托管 step 描述，周期等待由 SDK 负责；观察 srnos_example_create_result 检查创建结果。 旧 ThreadConfig.entry 不再支持；公共创建/停止/删除及同步用法见 Source/Kernel/README.md。本次编译不意味着重新执行板端测试。
