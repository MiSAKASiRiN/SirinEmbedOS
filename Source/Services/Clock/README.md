# 启动计时与绝对时间

SDK 保留两个独立时间概念：

- `SRNOS_TimeMs()`：32 位 RTOS 毫秒计数，约 49.7 天回绕，继续用于调度、TTL 和超时。
- `SRNOS_TimeUs()`：64 位 DWT 启动观察时间，供采样和时间映射使用。它与 TimeMs 原点不同，不能简单乘 1000 相互转换。DWT 在固定 CPU 时钟、正常运行与持续扩展计数的条件下计时；调试停核不是持续走时保证。
- `SRNOS_ClockGetSnapshot()`：一次取得同一时刻的 `uptime_us` 和可选 `unix_us`。
- `SRNOS_ClockGetUnixUs()`：只读取 Unix UTC 微秒；未授时返回 false，并将输出置零。

公共头为 `SirinEmbedOS/Services/Clock.h`。Unix 时间从 1970-01-01 UTC 起算，不加入北京时间偏移；时区由显示端处理。Clock getter 仅限任务上下文，短临界区复制，不阻塞、不分配内存。

## 状态与校时语义

每次 MCU 启动，`absolute_valid=false`。网络未开启、服务器未启动或授时失败都不会阻塞应用启动；启动计时始终继续。有效 NTP 回复建立 `Unix 基准 + 启动时间增量`，之后读取不需要访问网络。

授时成功后 `absolute_valid=true`。断网、请求超时或超过预计的下一次校时间隔时，`holdover=true`，仍按照本地晶振继续绝对时间；它表示最后授时基础上的估计，不代表仍与服务器同步。单次授时模式在成功后立即进入 holdover。查看 `last_sync_uptime_us` 可计算授时年龄。

每次有效授时更新 `generation`，记录 `last_correction_us`、NTP stratum、leap indicator 和往返时间。绝对时间允许向前或向后跳变；启动时间、控制周期和超时完全不受影响。当前没有 slew、频率驯服、RTC 或 EEPROM 恢复；MCU 重启必须重新授时。不能用绝对时间计算控制 dt。

`round_trip_us` 是扣除服务器处理时间后的软件往返时延，不是精度保证或协方差。软件收发排队与上下行不对称不能从一次交换中消除；leap indicator 只报告服务器状态，不实现本地闰秒调度。绝对时间准确性还取决于服务器本身。

## 使用

```c
#include <SirinEmbedOS/Kernel/Thread.h>
#include <SirinEmbedOS/Services/Clock.h>

void read_time(void)
{
    SRNOS_ClockSnapshot time;
    if (!SRNOS_ClockGetSnapshot(&time)) return;
    uint64_t uptime_us = time.uptime_us; // 即使未授时也可用
    (void)uptime_us;
    if (!time.absolute_valid) return;
    uint64_t seconds = time.unix_us / 1000000u;
    uint32_t nanoseconds = (uint32_t)(time.unix_us % 1000000u) * 1000u;
    (void)seconds; (void)nanoseconds; // 可编码为 ROS sec / nanosec
}
```

传感器在采集阶段保存 `SRNOS_TimeUs()`。稍后任务中取得 ClockSnapshot，使用
`SRNOS_ClockConvertUptime(&time, sample_us, &unix_us)` 将过去样本映射到 Unix 微秒。
函数拒绝未授时、未来采样时间及下溢；它不保存历次映射。需要跨校时边界重现同一时间的消费者应保留样本对应的 ClockSnapshot/generation，而不是重新解释历史样本。

数据协议应携带 `uptime_us`、绝对时间有效标志及 generation。未授时不能将 uptime 冒充 ROS Unix 时间；上位机可保留数据或自行建立时间映射。已经发布的样本不追溯重写。传感器内部滤波、FIFO、总线接收和处理延迟仍需独立标定。

## NTP 接入

[Network](../Network/README.md#ntp-授时) 提供兼容 Jetson chrony 的 SNTPv4 客户端。
服务器地址与周期在父项目配置，SDK 默认关闭。Clock 没有新线程，仅占用固定映射状态；接收更新由已有 Network worker 负责。
