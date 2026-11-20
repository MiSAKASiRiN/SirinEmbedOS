# W.BUS 接收

纯解码与流式组帧实现位于 `wbus_framer.c`，可独立链接到裸机固件。MP257 的实际用法见 [M0+ 接收](../../../../Board/MP257/CM0PLUS/README.md)。

公共头：`SirinEmbedOS/Services/RemoteControl/WBus/WBus.h`。用于 WFLY 接收机的 S.BUS 兼容输出，串口为 100000 bit/s、8 数据位、偶校验、2 停止位。直连本板 RX 时启用 `rx_inverted`；外部已经反相时应关闭。供电、电平和接收机输出模式需按实际硬件确认。

通道必须是 `SRNOS_SERIAL_DUPLEX` 且启用 RX；固定 RS485 的 UART7 不可用于 W.Bus。内置启动配置预检与 `SRNOS_WBusInit` 共用这一校验，不会把不匹配模式留到绑定阶段才发现。

25 字节格式：`0x0F`、22 字节低位优先打包的 16×11 bit 通道、标志、`0x00` 尾字节。标志 bit0/1 为数字通道 17/18，bit2 为 frame lost，bit3 为 failsafe；高四位必须为零。本模块不接收 S.BUS2 遥测尾字节。协议无 CRC，包头/尾和保留位检查不能排除所有载荷损坏。

格式依据：[WFLY ET08A 原厂手册的 RF206S 说明（镜像，第 37 页）](https://www.manualslib.com/manual/3701261/Wfly-Et08a.html?page=37) 和 [Bolder Flight Systems 的 S.BUS 协议说明](https://github.com/bolderflight/sbus)。实现独立编写，没有导入第三方代码。

## 接入

1. 在 `SRNOS_ApplicationConfigure` 中向已有 `serial` 配置追加端口，设置 100000/8E2、RX、正确反相；建议关闭 TX。
2. 静态零初始化 `SRNOS_WBus`。在 `bind_channels` 调用 `SRNOS_WBusInit(&receiver, SRNOS_SerialGetChannel(port))`；检查返回值。此时独占端口，不能同时绑定 DR16、VT03 或其他协议。
3. 单一应用任务每 10 ms 调用 `SRNOS_WBusPoll(&receiver, &latest, SRNOS_TimeMs())`。返回本次帧数，`latest` 为本次最后一帧。无新帧时保留输出，调用方必须区分陈旧值。

```c
#include <SirinEmbedOS/Services/RemoteControl/WBus/WBus.h>
#include <SirinEmbedOS/Kernel/System.h>

static SRNOS_WBus receiver;
static SRNOS_WBusDec latest;

static bool bind_wbus(void *unused)
{
    (void)unused;
    return SRNOS_WBusInit(&receiver,
        SRNOS_SerialGetChannel(SRNOS_SERIAL_USART6)) == SRNOS_SERIAL_OK;
}

static SRNOS_StepResult observe(void *unused, const SRNOS_ThreadWake *wake)
{
    (void)unused; (void)wake;
    if (SRNOS_WBusPoll(&receiver, &latest, SRNOS_TimeMs())) {
        /* latest.ch[0] is CH1. Log/store only; application chooses semantics. */
    }
    return SRNOS_STEP_CONTINUE;
}
```

纯解码 `Decode25` 和流式 `FramerFeed` 不依赖运行中串口，可用于离线测试。Framer 消费所有输入，逐字节重同步，计数所有帧并只返回最新值。Poll 每次最多处理 1024 字节；UART 错误、恢复或 RX 溢出会丢弃待处理数据和半帧；半帧超过 30 ms 未读到后续数据时清除。此时间基于任务取数，不能当成线上的到达时刻。

`ch[0..15]` 保留 CH1..16 的原始 0..2047 数值，不假设摇杆顺序、中心、端点或通道用途。`diag.last_frame_ms` 是最后解码时间；`seen_frame` 不会因失联自动清零。使用时结合帧年龄、`frame_lost`、`failsafe` 判断有效性。`framer` 提供帧数、丢弃字节数和失控/丢帧计数；`diag.serial` 保留 UART 错误与溢出统计。

独立解码器不创建线程、不写 UART，不提供控制映射或仲裁。可通过 `remote.wbus_port` 交给 SDK InputIO 轮询并发布 `wbus` 快照；也可由应用独占 Init/Poll。两种方式不可占用同一端口。内置绑定的校准放在 `remote.wbus_channels`，其他任务通过 InputReadSnapshot 读取；独立对象及诊断由其唯一轮询任务访问。Framer 的 frame_callback 在 Init 后设置，每个完整帧回调一次。绑定启动后不支持释放/重绑。

版本记录见 [CHANGELOG](CHANGELOG.md)。

内置 Input 适配层会丢弃仅带 `frame_lost` 的帧，不刷新有效字段/在线时间、不改变 Input epoch；解码器原始标志和累计计数继续保留。带 `failsafe` 的帧立即使来源失效，即使同时带 LOST；UART 流中断仍使来源失效。上述过滤属于内置适配层，独立 Decode/Framer/Poll 仍返回所有已解码帧，调用者自行检查标志。
