# 时间观测

`SirinEmbedOS/Diagnostics/Time/Time.h` 提供 DWT 时间观测和整数周期换算；`SirinEmbedOS/Kernel/Thread.h` 提供任务调度用的 `SRNOS_TimeMs()`、`SRNOS_Delay()`、`SRNOS_DelayUntil()`。公开头不包含 CMSIS，初始化由 SDK 完成，应用不重新设置 DWT 或 CPU 频率。

## 测量示例

```c
#include <SirinEmbedOS/Diagnostics/Time/Time.h>

uint64_t measure_work_us(void (*work)(void))
{
    const uint64_t start = SRNOS_TimeGetUptimeUs();
    work();
    return SRNOS_TimeGetUptimeUs() - start;
}
```

调用者传入非空、有界函数；结果是经过时间，包含中断、抢占与函数内等待，不能解释为任务 CPU 消耗。业务周期使用托管线程 `period_ms`；有限阶段等待用 Kernel 延时接口，示例见 [Kernel](../../Kernel/README.md#延时与时钟)。`TimeDelayUs/TimeDelayMs` 为底层忙等，不用于应用周期或服务空闲等待。

## 精度与范围

DWT 32 位短区间差只覆盖一次回绕；长时间测量使用每 tick 扩展的 64 位 uptime。整数 MHz 时钟下，微秒查询通过短中断屏蔽保护商/余数缓存，余数跨调用保留；频率变化或查询间隔超过 32 位周期时重新计算。TimeUs 观测时钟与 TimeMs 调度时钟起点不同，不能相减或混作绝对截止点。

维护时检查计数回绕、长查询间隔、频率重初始化及原中断屏蔽状态恢复。硬件时间与临界区成本需按实际构建/负载测量，历史数据及证据链接见 [CHANGELOG](CHANGELOG.md)。
