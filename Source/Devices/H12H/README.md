# H12H 串行舵机协议

公共头为 `SirinEmbedOS/Devices/H12H/H12H.h`。一个 `SRNOS_SerialServoH12h` 实例服务同一 Serial 通道上的多个舵机 ID。父项目在 `SRNOS_ApplicationConfigure()` 声明串口参数，在 `bind_channels` 回调中获取未占用的通道并初始化协议实例；对象与通道句柄保持到固件使用结束。完整串口配置见 [Serial](../../IO/Serial/README.md)。

```c
#include "SirinEmbedOS/Devices/H12H/H12H.h"
static SRNOS_SerialServoH12h servo_bus;

// 由 bind_channels 调用；USART6 必须已在 SystemConfig.serial 中声明。
int bind_servo_bus(void) {
    SRNOS_SerialChannel *channel = SRNOS_SerialGetChannel(SRNOS_SERIAL_USART6);
    if (!channel) return SRNOS_SERIAL_SERVO_H12H_ERR_ARG;
    return SRNOS_SerialServoH12hInit(&servo_bus, channel, 30);
}
// 初始化成功且调度启动后，在任务中读取；不触发舵机动作。
int read_servo_position(int16_t *position) {
    return SRNOS_SerialServoH12hReadPosition(&servo_bus, 1, position);
}
```

初始化函数负责 Claim 通道，不应由应用提前重复 Claim。事务限任务上下文，超时单位为毫秒，发送/应答期限遵循 Serial Transceive 合同。位置原始范围为 `0..SRNOS_SERIAL_SERVO_H12H_POS_MAX`，转换角度使用 `SRNOS_SerialServoH12hPositionToDeg()`。单线舵机需要已确认的电气适配器；SDK 不自动将普通 UART 改成单线。

硬件时序、外部舵机应答与动作需在实际接线下验证。
