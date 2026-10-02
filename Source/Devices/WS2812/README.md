# WS2812 驱动

公开接口为 `SirinEmbedOS/Devices/WS2812/WS2812.h`，使用 `SRNOS_WS2812*` 和 SDK 结果类型；父项目不需要 HAL 头。初始化、DMA scratch、IRQ 和 critical 阻塞发送属于私有实现。固定 PA10/TIM1/DMA1 Stream1 的绑定分别来自 `Board/Support/Devices/ws2812_hardware.h`、`ws2812_critical_hardware.h`。普通路径 DMA request 为 TIM1_UP，critical 路径为 TIM1_CH3，原电气时序不变。

SetPixel/SetAll/亮度调整后由 CommitStartAsync 非阻塞提交；调用者协调与 Indicators 的资源所有权，BUSY 时不改写正在传输的内容。完成/错误及通知回调位于 DMA ISR，不能阻塞，应用通知只使用 SDK 的 ISR 接口。回调描述提交时复制，用户上下文保持到完成；没有向应用暴露 DMA buffer。

普通 IRQ 由 Board/Support 转接；critical 为无 IRQ 阻塞发送。两条路径使用同一资源，不代表支持同时运行；启动/心跳策略属于 Services/Indicators。主机测试和构建不证明物理灯效。
