# Changelog

## Unreleased

- 2026-09-29：首次实板验证参数保存/复位加载、SAVE+WRITE代次、8路遥测与断连；校准静止误拒绝及实际遥测吞吐缺口保留。未启用CAN，EEPROM已恢复原内容，见 [记录](../../../Docs/Validation/PersistenceBoard-20260929/README.md)。

- 增加显式解锁的单次 SAVE/LOAD、校准重启、八路最新值遥测和可选 CAN1/CAN3 组合负载入口；普通启动不写介质或发送 CAN。本轮仅构建，实际断电、总线负载与持久化板测待授权后执行。

## 2026-10-17 文档整理

- 使用说明保留当前合同、入口和示例；日期战报与修改记录归入本文件。仅调整 Markdown，代码、原始测量数据和第三方源码未改动。

### README.md 中保留的历史说明

以下内容描述原文所述时期，不作为当前用法；当前入口见 [README.md](README.md)。

2026-09-29 首次实板记录见 [PersistenceBoard](../../../Docs/Validation/PersistenceBoard-20260929/README.md)：参数保存/重启、代次隔离通过；校准静止误拒绝和实际遥测速率未达目标仍待处理。未发送CAN帧，原EEPROM已恢复。

HostComm SOURCE V2（TELEMETRY 0x10–0x14）可订阅八源各 1000 Hz；停读、断连、重连并观察回传的采样时间/序号和跳过计数。没有订阅时生产者仍更新缓存，不发送数据。本轮不提供上位机产品，协议编码可复用主机测试或独立测试客户端。
