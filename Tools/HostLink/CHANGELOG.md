# Changelog

## Unreleased

- 2026-09-29：文档明确现有 CLI 支持 IMU/INS/监测接收保存，不包含参数编辑或静态校准触发界面；与当前固件协议入口互链。


- 2026-09-28：修复主机把 SAMPLE message=0x02 误写为 0x03 导致三流静默过滤的问题，同步回归与协议说明；实板接收、背压及 DTR 重连通过。

- 2026-09-28：查看/保存工具支持循环统计 0x0003 和旧双流固件，展示统计口径并保存实际支持流信息。 主机/构建证据见 `Docs/Validation/ManagedTasks-20260928/README.md`；本次未烧录，时序与开销仍待板测。

- 2026-09-28：SystemMonitor替代Diagnostics线程，1 Hz ADC/任务统计与HostLink发布；增加ADC板级BDMA接入、逐槽统计和主机工具，默认取消扫栈。迁移system_monitor_enabled及HostLinkPrepareEx合同；Indicators/生成文件不变。主机与构建验证见Docs/Validation.md，未烧录/板测。

## 2026-10-17 文档整理

- 使用说明保留当前合同、入口和示例；日期战报与修改记录归入本文件。仅调整 Markdown，代码、原始测量数据和第三方源码未改动。

### README.md 中保留的历史说明

以下内容描述原文所述时期，不作为当前用法；当前入口见 [README.md](README.md)。

## 2026-09-28 板端验证

SystemMonitor SAMPLE 帧使用 kind=2、service=4、message=2；流 ID 位于 payload，循环流 0x0003 不是 message=3。板端已验证三条流接收和 DTR 重连。
