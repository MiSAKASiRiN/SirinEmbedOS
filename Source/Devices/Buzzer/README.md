# 蜂鸣器驱动

提供频率/音符播放、队列及 ISR 入队 API，硬件采用固定 TIM2/DMA1 Stream0 驱动 GPIOE13。定时器和 DMA 私有 handle、队列及波形实现保留在设备驱动；固定硬件宏由 Board/Support/Devices/buzzer_hardware.h 私有提供。公共头仅提供 SRNOS_BuzzerStatus、SRNOS_BuzzerNote 和 SRNOS_Buzzer* 播放/队列接口，不包含 HAL/RTOS 头；Init、PortHooks、Service 与工作唤醒接口在 buzzer_service.h 私有维护。父项目不初始化硬件，也不手动轮询 Service。队列容量由 `SRNOS_BUZZER_QUEUE_CAPACITY` 决定（环形队列保留一槽），不支持应用覆盖 GPIO/TIM/DMA 布线。

DMA 入口由 Board/Support/Devices/indicators_board.c 转接，启动音和周期服务策略属于 Services/Indicators。主机服务测试与固件编译不代替实板发声检查。

Indicators 按下一音符截止点推进队列；任务或 ISR 入队/停止在临界区外唤醒工作线程。普通播放限任务上下文，`FromISR` 仅供符合 SDK RTOS 调用条件的中断（当前优先级数值 5～15）使用，不能阻塞。

## 播放示例

在 SDK 完成指示器初始化后调用；入队成功只表示队列接纳，队列满时由业务决定丢弃或稍后重试。音符时长单位为毫秒，频率 0 表示休止。

```c
#include "SirinEmbedOS/Devices/Buzzer/Buzzer.h"
SRNOS_BuzzerStatus play_short_beep(void) {
    const SRNOS_BuzzerNote note = {880, 80};
    return SRNOS_BuzzerEnqueue(note);
}
```

需要中止队列时使用 `SRNOS_BuzzerFlush()`，直接持续音使用 `Start()`/`Stop()`；协调系统启动音和应用提示音的使用时机。维护入口与验证分层见 [验证指南](../../../Docs/Validation.md)。
