# 调试观测记录

公共头 `SirinEmbedOS/Diagnostics/Watch/Watch.h` 定义固定调试布局、模块 ID 和 `SRNOS_Debug*` 记录。记录用于匹配 ELF 的调试器观测；业务代码优先使用各模块的副本接口，不把调试器读取 volatile 当成跨字段一致快照。

## 调试器示例

加载与板端固件完全一致的 ELF，完成连接后在 GDB 控制台按符号查询：

```gdb
p SRNOS_DebugDbgMpuDcache.h
p SRNOS_DebugDbgMpuDcache.itcm_bss_size
p SRNOS_DebugLog.dropped_queue
p SRNOS_DebugHostLink.transport_disconnects
```

确认 header 的 magic、version、size 和 module_id，再按该 ELF 对应头文件解码；字段值须结合采样时刻和运行状态判断。暂停目标会改变任务时序，不能用暂停期间数据证明实时性能。不要硬编码旧地址，也不要在早期内存初始化前读取这些记录。

## 内存与维护

`SRNOS_DEBUG_WATCH_BSS` 放入 ITCM `.debug_watch`，启动时显式清零；普通变量不会仅因名称含 Debug 自动迁移。Watch、CAN Router BSS 和代码共享 64 KiB ITCM，增大记录需检查 Debug/Release map 和链接断言。本模块没有 worker、日志或遥测依赖。

`SRNOS_DbgMpuDcache` 的当前 `header.size` 为 72 B，包含 ITCM code/BSS 字节数；解析器以当前结构大小及版本校验，不假定所有记录等长。历史布局与验证证据见 [CHANGELOG](CHANGELOG.md)和 [ITCM 记录](../../../Docs/Validation/ITCM-20260929/README.md)。
