# 时间观测

`SirinEmbedOS/Diagnostics/Time/Time.h` 提供 `SRNOS_Time*` DWT 观测和整数周期换算；`SirinEmbedOS/Kernel/Time.h` 提供父项目调度时钟、SRNOS_Delay/DelayUntil。公开头不包含 CMSIS，寄存器访问在实现内。

业务等待使用 Kernel 延时接口；TimeDelayUs/TimeDelayMs 是底层精确忙等，不能用于应用周期或服务空闲等待。DWT 32 位短区间差支持一次回绕，长时间使用由 tick 扩展的 64 位 uptime。CPU 周期计时包含中断与抢占，不等同于线程 CPU 消耗。

本轮为源码命名与依赖隔离，接口/构建检查见 `Docs/Validation/SRNOSBoundary-20260928/README.md`；没有新增板端验证。
