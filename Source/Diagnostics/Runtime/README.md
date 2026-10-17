# 运行观测基础设施

本目录只负责轻量 trace、DWT 运行时计数及逐任务槽取样，不创建诊断线程，不依赖 HostLink/日志。主动采样、统计窗口和发布由 Services/SystemMonitor 持有。FreeRTOS 宏唯一入口为私有 trace_internal.h，经 Trace.c 同时维护 Fault 元数据与本模块槽位。

私有 runtime_trace.c 维护 create/delete/ready/switch 计数。槽位 1..64，每次复用增加 generation；活跃句柄只能在 SRNOS_RuntimeSampleSlot 的临界区内使用，一次处理一个槽且不扫描栈、不整表暂停调度。返回纯值而非句柄。Ready/max-run 语义和回绕限制见 SystemMonitor README。

SystemMonitor 禁用时 trace 和每 tick DWT 64 位扩展仍工作，Fault 与时钟不依赖后台采样线程。配置 INCLUDE_xTaskGetIdleTaskHandle 启用上游 getter，不修改 FreeRTOS 源码。主机测试用可删除的假任务检查句柄生命周期、运行窗口与一次 CYCCNT 回绕；实机临界区测量见下述板端记录。

## 托管循环统计

Kernel 的私有循环边界调用 cycle_statistics：开始/结束仅记录 DWT 原始周期、整数累计和极值，不分配、不日志、不发送、不扫栈。固定 64 槽累加器在启用时于调度前一次分配（目标 7168 B，预算 8 KiB），运行期不扩容。槽/generation 隔离生命周期；SystemMonitor 每槽短临界区取出并重置窗口，在临界区外换算耗时和平均值。未完成轮次在完成时归入窗口，因此执行墙钟总和不等同于窗口 CPU 时间。

周期统计包括完成轮数、执行墙钟 min/mean/max、相对释放点的启动延迟、相邻开始间隔减目标周期的有符号偏差/绝对偏差、截止时间未满足和跳过数。`MEASURED` 和 `JITTER_VALID` 分别表示有完成轮次/间隔样本，`PERIODIC` 才适用周期字段；事件线程无周期。普通等待、初始化及清理排除，处理内阻塞、抢占和 ISR 保留。Idle/Timer 只有调度基础统计，没有循环统计。ApplicationMain 任意自定义循环同样不作推断。

启动配置 cycle_statistics_enabled 可关闭 hook；不改变服务等待或调度。统计 hook 成本字段为诊断估计，包含发生期间的抢占，不包含最终成本记账/返回；不能代替无抢占基准。最大临界区同时汇入 SystemMonitor。板端测量与适用负载见 [验证记录](../../../Docs/Validation/ManagedTasksBoard-20260928/README.md)，不把一次测量作为所有负载的保证。

Release 单独对循环统计源文件使用 `-O2`（其他文件保留原工具链选项）。hook 直接读取已初始化的 CYCCNT，在短临界区内基于现有每 tick trace 刷新的 64 位锚点扩展时间，避免反复调用公共 DWT 懒初始化和嵌套 PRIMASK；事件任务不计算周期字段。跨多个计数器回绕的等待仍由每 tick 的绝对周期更新保持连续。Debug 保留原调试优化级别。

## 公共隔离

FreeRTOS trace 入口位于私有 `trace_internal.h`，父项目不包含 Trace 或内核头。公开任务/循环观测统一经 SystemMonitor 副本接口；Watch/时间自维护符号全部采用 SRNOS 命名，已有记录 magic 和结构值保留。


## 应用读取示例

父项目不直接调用私有 trace/hook；通过 SystemMonitor 读取副本，并检查当前窗口是否有效：

```c
#include <stddef.h>
#include <SirinEmbedOS/Services/SystemMonitor.h>

bool read_execution_peak(uint8_t slot, uint32_t *peak_us)
{
    SRNOS_MonitorTask task;
    if (peak_us == NULL || !SRNOS_SystemMonitorGetTask(slot, &task)) return false;
    const uint32_t needed = SRNOS_CYCLE_ACTIVE | SRNOS_CYCLE_MEASURED;
    if ((task.cycle.flags & needed) != needed ||
        (task.cycle.flags & SRNOS_CYCLE_STALE)) return false;
    *peak_us = task.cycle.execution_max_us;
    return true;
}
```

调用者每次保存 slot 和 generation，避免将槽复用后的新任务混入旧统计。上述值包含 step 内阻塞和抢占；CPU 占比使用任务自己的 CPU 有效位与 `cpu_ppm`。`stack_free_words == UINT16_MAX` 表示未测量，不能计算实际栈余量。监测开关、窗口和完整示例见 [SystemMonitor](../../Services/SystemMonitor/README.md)。
