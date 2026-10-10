# 时间观测

`SirinEmbedOS/Diagnostics/Time/Time.h` 提供 `SRNOS_Time*` DWT 观测和整数周期换算；`SirinEmbedOS/Kernel/Time.h` 提供父项目调度时钟、SRNOS_Delay/DelayUntil。公开头不包含 CMSIS，寄存器访问在实现内。

业务等待使用 Kernel 延时接口；TimeDelayUs/TimeDelayMs 是底层精确忙等，不能用于应用周期或服务空闲等待。DWT 32 位短区间差支持一次回绕，长时间使用由 tick 扩展的 64 位 uptime。CPU 周期计时包含中断与抢占，不等同于线程 CPU 消耗。

本轮为源码命名与依赖隔离，接口/构建检查见 `Docs/Validation/SRNOSBoundary-20260928/README.md`；没有新增板端验证。


整数MHz时钟的微秒查询采用受短中断屏蔽保护的商/余数缓存，避免共享SPI高频路径重复64位除法。余数跨调用保留，不累计舍入误差；频率变化或两次微秒查询相距超过32位周期时重新计算绝对商/余数。DWT扩展、tick维护和公开时间单位不变。实际实现主机回归覆盖反复DWT回绕、长查询间隔、1..550MHz重初始化及原中断屏蔽状态；板端保护线按CAN4优化记录验收。

## 2026-10-06 优化后验证

Classic 1M/8B双向各3k完成3×60s＋600s，零丢帧/拒绝/TTL过期/硬件RX溢出，78个十秒CPU窗口最高53.9632%。双IMU/INS保持1kHz，含6k过载的完成间隔/采集/SPI排队峰值1143/151/112µs；FD五速率320格式用例和过载恢复通过，64B各3k在5/8M下CPU约61%。实测仅CAN1/CAN4短接，CAN2/3/UART同时满载与物理故障尚未覆盖；原Flash恢复并释放探针。详见 [最终验证与原始证据](../../../Docs/Validation/CAN4-Optimization-20261006/README.md)。
