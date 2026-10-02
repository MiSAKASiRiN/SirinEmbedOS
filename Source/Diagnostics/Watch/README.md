# 调试观测记录

公开头 `SirinEmbedOS/Diagnostics/Watch/Watch.h` 提供 SRNOS 命名的固定调试布局、模块 ID 与记录。带 `SRNOS_DEBUG_WATCH_BSS` 的记录位于 ITCM `.debug_watch`，绕过 D-cache，启动时用对齐的 64 位写入显式清零。按匹配 ELF 的符号访问，不硬编码原 D2 地址。普通业务诊断变量不因符号名称带 Debug 就自动迁入。

自维护导出符号使用 SRNOS_Debug 前缀。父项目常规运行状态优先使用各能力的副本快照，不把调试器直接读取 volatile 当成跨字段一致快照。此模块没有 worker、日志或遥测依赖。

CAN Router BSS、Watch 与已有代码共享 ITCM 64 KiB；Debug/Release 链接断言与 ELF 检查核对边界。`SRNOS_DbgMpuDcache` 末尾新增 ITCM code/BSS 字节数，header.size 从 64 改为 72，其余记录格式和 HostLink 线协议保持不变。禁止在 main 早期初始化之前使用这些记录。

验证范围见 [ITCM 迁移记录](../../../Docs/Validation/ITCM-20260929/README.md)。
