# CHANGELOG

## 2026-09-28

- 当前固件两组 ON/OFF 复测和 ADC/CDC 注入结果见 Docs/Validation/SRNOSBoard-20260928；首次 24 窗口预采集中断，排除 CPU 比较，随后四次完整窗口通过。最终恢复正常 Release 并释放探针。

- 公共依赖隔离后将内核测量和 ADC 注入装配到独立 OBJECT 目标；应用编译仍只获得 SDK 公共 include。Release 优化加调试信息的统计开/关固件均通过编译。

- 新增独立父项目板测：协作生命周期、TLS、同步删除保护、资源回收、统计开关 CPU 对照及完整 hook 基准。
- 新增仅测试固件链接的 ADC 完成/超时/晚处理/停止确认故障注入，以及只读遥测背压/DTR 重连工具。
- 实测证据与范围见 Docs/Validation/ManagedTasksBoard-20260928；不把主机测试或软件注入作为物理故障/外部 CAN 压力证明。

## 2026-10-17 文档整理

- 使用说明保留当前合同、入口和示例；日期战报与修改记录归入本文件。仅调整 Markdown，代码、原始测量数据和第三方源码未改动。

### README.md 中保留的历史说明

以下内容描述原文所述时期，不作为当前用法；当前入口见 [README.md](README.md)。

具体板端结果和未测边界见 `Docs/Validation/ManagedTasksBoard-20260928/README.md`。仪表精度对照、外部 CAN 持续负载、真实 ADC 硬件故障及长时间压力需要额外实验。
