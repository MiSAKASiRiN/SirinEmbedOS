# Changelog

## Unreleased

- 2026-09-29：首次实板验证参数保存/复位加载、SAVE+WRITE代次、8路遥测与断连；校准静止误拒绝及实际遥测吞吐缺口保留。未启用CAN，EEPROM已恢复原内容，见 [记录](../../../Docs/Validation/PersistenceBoard-20260929/README.md)。

- 增加显式解锁的单次 SAVE/LOAD、校准重启、八路最新值遥测和可选 CAN1/CAN3 组合负载入口；普通启动不写介质或发送 CAN。本轮仅构建，实际断电、总线负载与持久化板测待授权后执行。
