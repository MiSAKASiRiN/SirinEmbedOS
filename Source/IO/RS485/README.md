# RS485 事务与路由

公共接口为 `SirinEmbedOS/IO/RS485/RS485.h` 和 `Modbus/ModbusRTU.h`。本板固定使用 UART7、硬件 DE，发送期间关闭 RE 抑制本机回显，在 UART TC 后恢复接收。串口参数由 `SystemConfig.serial` 声明，端点在 `bind_channels` 中注册；配置启动后冻结，不支持运行期重绑。

一个端点持有一个 RAW 或 Modbus 协议地址。同一总线使用 `SRNOS_RS485` 事务线程（优先级 47、3 KiB 栈）；没有端点时不创建该线程。队列等待位于周期统计之外，应答/静默等待计入事务墙钟。应用不另行启动 `SRNOS_RS485Start()` 或直接争用 UART7。

## RAW 端点示例

以下片段在 `bind_channels` 注册，运行期任务发送。`rx` 指针和 `frame->data` 仅在回调期间有效，需要留存时复制到应用有界缓冲。

```c
#include "SirinEmbedOS/IO/RS485/RS485.h"
static SRNOS_RS485RouterHandle endpoint;
static int owner;
static void received(SRNOS_RS485RouterHandle handle,
                     const SRNOS_RS485RouterRxFrame *frame, void *user) {
    (void)handle; (void)frame; (void)user; // 替换为有界处理，不阻塞
}
int bind_raw_endpoint(void) {
    return SRNOS_RS485RouterRegisterRaw(SRNOS_RS485_ROUTER_PORT_UART7,
                                       &owner, 0, received, 0, &endpoint);
}
int send_raw(const uint8_t *bytes, uint16_t length) {
    return SRNOS_RS485RouterSendRaw(endpoint, bytes, length, 20);
}
```

上例尚需在 `SystemConfig.serial` 设置 UART7、RS485 模式与真实波特率；完整声明方式见 [Serial](../Serial/README.md)。发送成功表示复制入队，不能当作外设应答。RAW 被动输入最多按 256 B 分片；回调按流协议处理，不能把一次回调默认当成一整帧。

Modbus 事务检查地址、功能码、长度和 CRC；广播不等待响应。低速至少保持 3.5 字符静默，高于 19200 时至少 1750 µs，1 kHz 内核按 tick 向上取整。发送的超时限制队列等待，应答期限在配置阶段声明；超时不会撤回已发出的字节。TL42R 接入见 [设备示例](../../Devices/TL42R/README.md)。

物理 DE/RE、总线极性、终端与设备响应时序需实机验收。
