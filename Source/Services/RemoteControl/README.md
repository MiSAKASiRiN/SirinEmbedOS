# 物理遥控输入

`remote.dr16_port`、`remote.vt03_port`、`remote.wbus_port` 在启动时绑定 Serial 并自动注册独立 Input 来源。运行期通过 [Input](../Input/README.md) 的快照/事件接口读取。SDK 不选择活动遥控器，不配置优先级、死区、底盘方向或控制模式。

| 来源名 | 接收 | 通道布局（零起始） | 按键/增量 |
|---|---|---|---|
| dr16 | 18 B DBUS，100000/8E2 | 0…3 摇杆，4 AUX 原始值，5/6 为 s1/s2 的原生 1上/3中/2下 | 16 位键盘，其中既有 DR16 低八位 W/S/A/D/Q/E/Shift/Ctrl 转为 Input 的低八位；高八位原位保留。鼠标 L/R 位16/17，相对 xyz。 |
| vt03 | VT03/13 21 B 遥控帧，921600/8N1 | 0…3 摇杆，4 wheel，5 gear（0/1/2 原值） | 键盘位0…15，鼠标位16…18，Pause/Fn1/Fn2/Trigger 位19…22，相对 xyz。 |
| wbus | 25 B W.Bus，100000/8E2 | 0…15 原始 11 位通道，按 remote.wbus_channels 逐通道可选校准 | 数字 CH17/18 位32/33；仅 frame_lost 丢弃本帧、不刷新 Input；failsafe 立即使整个来源无效。 |

DR16 的 0…3、VT03 的 0…4 按 364/1024/1684 归一化。DR16 AUX 的可接受原始范围更宽，默认保留 raw，避免把端点假定为普通摇杆。W.Bus 默认没有归一化配置。键位按接收协议合同处理；应用不应把未知/未使用通道当作有效控制指令。

`remote.offline_ms` 默认 300 ms，范围 1…60000；Input 对字段检查新鲜度。VT03 的自定义 A5/0311 数据走 Message，不能刷新物理 vt03 的在线状态。开启 video_link_enabled 必须 VT03 通道为 RX/TX 全双工，详见 [VideoLink](../VideoLink/README.md)。

W.Bus 的单次 `frame_lost` 仅由解码器累计计数，不发布该帧的通道/按键，也不改变 Input sequence、epoch、事件或时间戳。此前有效字段按原时间自然过期；连续 LOST 不会延长在线或控制许可。`failsafe` 优先于 `frame_lost`，两标志同时置位也立即发布失效；正常帧恢复会改变 epoch，应用仍需执行自己的重新使能要求。UART 错误、溢出和流重置依然发布 INPUT_FRAME_LOST 使来源失效，与接收机单帧 LOST 不同。更严格的控制超时（例如父项目 50 ms）由消费者在原有效字段时间上判断。

DR16/VT03/W.Bus 解码器均保留独立公开接口，允许应用在未启用内置绑定时自行 Init/轮询同一协议的另一实例。端口只有一个所有者；使用内置来源时不能再由父项目 Init/Poll 同一端口。DR16/VT03 Config 的 frame_callback 每个解码帧调用一次，W.Bus Framer 的回调在 Init 后设置；回调借用帧，仅在接收任务内有效。每帧发布可保留同次 Poll 中按下后立即松开的边沿与相对增量，不把多个帧压成一个按键状态。

InputIO 只在有物理接收器或消息 UART 时创建，优先级 47、3072 B 栈、每轮相对等待 1 ms。Serial 负责 DMA/错误恢复；检测字节流溢出/硬件错误时清除未完成帧并使来源失效，后续完整有效帧恢复。每次 Poll 有读取上限，不因持续输入而无限运行。DR16 没有 CRC，按字段范围/帧长度同步的能力有限，不能提供强完整性保证。

完整配置示例见 [InputMessages](../../../Examples/InputMessages/README.md)，原板端口参考见 [PrototypeBindings](../../../Examples/PrototypeBindings/README.md)，W.Bus 独立解码接口见 [WBus](WBus/README.md)。
