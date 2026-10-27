# WS2812 驱动

公开接口为 `SirinEmbedOS/Devices/WS2812/WS2812.h`，使用 `SRNOS_WS2812*` 和 SDK 结果类型；父项目不需要 HAL 头。初始化、DMA scratch、IRQ 和 critical 阻塞发送属于私有实现。固定 PA10/TIM1/DMA1 Stream1 的绑定分别来自 `Board/Support/Devices/ws2812_hardware.h`、`ws2812_critical_hardware.h`。普通路径 DMA request 为 TIM1_UP，critical 路径为 TIM1_CH3。

SetPixel/SetAll/亮度调整后由 CommitStartAsync 非阻塞提交；调用者协调与 Indicators 的资源所有权，BUSY 时不改写正在传输的内容。完成/错误及通知回调位于 DMA ISR，不能阻塞，应用通知只使用 SDK 的 ISR 接口。回调描述提交时复制，用户上下文保持到完成；没有向应用暴露 DMA buffer。

普通 IRQ 由 Board/Support 转接，启动/心跳策略属于 Services/Indicators。私有 critical 路径作为备用实现保留，当前启动、心跳和 Fault 流程均未调用；它采用无 IRQ 阻塞发送，并与普通路径共用硬件，接入时必须确保单一所有权。主机测试和构建不证明物理灯效。

## 自定义灯色示例

以下片段仅在应用已协调好 Indicators 的单一写入所有权后调用；`IsBusy()` 不是多任务锁，多个生产者须由应用串行化。LED 索引从 0 开始，RGB 和亮度范围为 0..255。

```c
#include "SirinEmbedOS/Devices/WS2812/WS2812.h"
SRNOS_WS2812Result set_green(void) {
    if (SRNOS_WS2812IsBusy()) return SRNOS_WS2812_BUSY;
    SRNOS_WS2812SetGlobalBrightness(16);
    SRNOS_WS2812SetAll(0, 255, 0);
    return SRNOS_WS2812CommitStartAsync(0); // 不注册 ISR 回调
}
```

`OK` 表示提交成功，实际结束用 `IsBusy()` 或完成回调判断。不要在忙等待循环里持续轮询；周期任务可在下一轮检查。
