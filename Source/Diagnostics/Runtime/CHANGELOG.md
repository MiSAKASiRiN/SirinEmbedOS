# Changelog

## Unreleased

- 2026-09-28：FreeRTOS trace 入口位于私有 `trace_internal.h`，父项目不再包含 Trace 或内核头。公开任务/循环观测统一经 SystemMonitor 副本接口；Watch/时间自维护符号全部采用 SRNOS 命名，已有记录 magic 和结构值保留。 本轮主机/构建范围见 `Docs/Validation/SRNOSBoundary-20260928/README.md`；未重新烧录。

- 2026-09-28：trace_internal.h 归实现私有，FreeRTOS 原生 trace 宏不再进入公共头；Fault 与运行计数分发保持。

- 2026-09-28：根据实板开销优化循环 hook：Release 局部 -O2、直接读取计数器、复用每 tick 64 位锚点、跳过事件任务周期计算；增加多次回绕睡眠测试。完整开关对照见板测记录。

- 2026-09-28：增加按生命周期隔离的托管循环整数 hook、窗口换算和成本记录。 主机/构建证据见 `Docs/Validation/ManagedTasks-20260928/README.md`；本次未烧录，时序与开销仍待板测。

- 2026-09-28：移除旧 HSKP/Diagnostics worker 及公开入口，保留内部 trace；增加槽位 generation 和逐槽短临界区采样，句柄不离开保护区。
- 活跃线程的汇总移到 SystemMonitor；默认不再周期扫描栈，Fault、DWT 和栈溢出检测不变。
- 主机测试覆盖创建/删除/复用、计数回绕和受保护的状态读取；板端开销仍待验证。
