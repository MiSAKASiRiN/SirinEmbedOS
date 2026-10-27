# 指示器服务

SDK 负责固定蜂鸣器/LED 初始化、启动提示与心跳；应用通过公共 `SRNOS_Buzzer*`、`SRNOS_WS2812*` 接口使用设备，不重复创建 worker 或调用私有 `indicators_internal.h`。

## 使用示例

在已有配置入口设置 `config->bringup_indicators = false` 可关闭启动灯效与提示音；设备仍初始化，蜂鸣器队列仍被服务。应用任务中提交一个有限音符：

```c
#include <SirinEmbedOS/Devices/Buzzer/Buzzer.h>

SRNOS_BuzzerStatus play_notice(void)
{
    const SRNOS_BuzzerNote note = { .freq_hz = 1500, .dur_ms = 80 };
    return SRNOS_BuzzerEnqueue(note);
}
```

检查返回值，队列满时由应用决定丢弃或稍后重试；不要在控制周期等待整段旋律。ISR 使用明确的 `EnqueueFromISR`/`FlushFromISR`。蜂鸣器和 LED 的详细合同见 [Buzzer](../../Devices/Buzzer/README.md)、[WS2812](../../Devices/WS2812/README.md)。

## 调度与资源

`SRNOS_Indic` 使用优先级 8、1024 B 栈，按下一音符或 500 ms 心跳截止点等待；无待办时无限等待，新请求通过通知唤醒。到期但 LED DMA busy 时一个 tick 后重试，延迟唤醒不补发积压心跳。普通等待不计入循环执行统计，处理内中断/抢占计入墙钟。

启用启动提示时，第一灯提交黄色，依次播放 1000/1500/2000 Hz 各 80 ms；初始化/输出失败只计数，不阻止启动。硬件与 DMA 接线在 Board/Support/Devices。`SRNOS_IndicatorsState` 可用于匹配 ELF 的调试器观测，编译与计数不能代替实际声光验收。

## 诊断

修改及历史验证见 [CHANGELOG](CHANGELOG.md)。
