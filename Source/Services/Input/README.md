# Input 与 SirinRMT

公开入口为 `Services/Input.h` 和 `Services/SirinRMT.h`。Input 保存每个输入设备的状态及明确的使能/禁用请求意图，不选择活动遥控器，不选择机器人模式、不做安装映射或速度限制，也不授予执行器权限。应用按名称取得来源句柄，各任务可以独立读取同一来源；来源优先级、安全门与控制映射属于父项目。用 InputCount/Describe 枚举已注册来源及其能力，用 SRNOS_INPUT_KEY_*、MOUSE_* 等枚举和 INPUT_BUTTON_MASK 构造可读按键掩码。

## 输入量

| 数据 | 快照与事件 | 单位及有效性 |
|---|---|---|
| 通道 | `raw[16]`、`normalized[16]` | 原始有符号整数；可选归一化为 -10000…10000，即 -100.00…100.00%。拨杆位置代码也放在通道中。每个通道独立时间戳/有效位。 |
| 按键 | 64 位 `buttons` 与 `buttons_valid`；事件中的 pressed/released | held 是状态；pressed/released 是状态变化。恢复连接或有效位变化产生 SYNC，未知旧状态不制造边沿。 |
| 平面速度 | `planar_velocity`、`planar_velocity_ms`、`planar_velocity_valid` | vx 前向 m/s、vy 左向 m/s、wz 俯视逆时针 rad/s；参考点为所控制刚体的声明中心，三分量完整更新。 |
| 相对增量 | 三轴 `relative_total`；事件中的 `relative[3]` | 鼠标等原生计数。累计值按 uint32 模 2^32 相加；用两次累计值之差或消费事件，不能每周期重用上次 delta。 |

快照包含 coherent 的数据、`online`、`seen`、`flags`、`sequence`、`epoch` 和字段有效位。`online` 只表示最近收到无失控标志的输入，不代表所有通道都有效。失联/失控时旧 raw 值仍供诊断，消费者必须检查对应 valid 位。自定义业务消息不会刷新任何物理遥控器的时间戳。

归一化由每通道的 minimum/center/maximum 校准决定，严格递增才产生 normalized_valid；零初始化表示只有 raw。支持不对称行程和 inverted。RF206S 的已测 CH5 可显式配置 `{353,1024,1694,SRNOS_INPUT_CHANNEL_AXIS,false}`；不要把某个接收机某通道的校准假设应用于所有 W.Bus 通道。死区、三挡阈值和机器人动作留给应用。

## 读取与注册

内置来源及通道布局见 [RemoteControl](../RemoteControl/README.md)。完整可构建例见 [InputMessages](../../../Examples/InputMessages/README.md)。任务内读取片段：

```c
SRNOS_InputSource source = SRNOS_InputFind("wbus");
SRNOS_InputSnapshot s;
if (SRNOS_InputReadSnapshot(source, &s) == SRNOS_INPUT_OK &&
    s.online && (s.normalized_valid & (1u << 4))) {
    float ch5_percent = s.normalized[4] / 100.0f;
    /* 应用在自己的周期中使用 ch5_percent。 */
}
```

应用输入适配器可在 ApplicationConfigure 或 bind_channels 中 `InputRegister`，描述会复制；每来源只允许一个任务生产者，在任务内 `InputPublish`。时间必须来自 SRNOS_TimeMs，禁止混用 TimeUs/1000；部分更新只刷新传入字段。InputInvalidate 使整个来源失效。运行期接口拒绝 ISR；查找和 Describe 读取冻结元数据。

每来源有 8 项事件环，支持多个读者各自持有 cursor。读取快照后把 cursor 初始化为 snapshot.event_sequence 可从“现在”开始。OVERRUN 将 cursor 移到最早保留事件之前，不写 out；下一次读取继续。消费者遇到 SYNC、OVERRUN、epoch 改变或来源离线时应重建本地状态。超时在读快照时计算，不主动生成“所有键释放”的假事件；离线必须检查快照。事件环不能恢复上游丢帧。

最多 8 个来源，启动注册后冻结。描述是静态元数据；快照和事件环在 osKernelInitialize 后按注册数量一次分配，运行期不分配。读取/发布仅在短临界区复制固定大小数据，归一化在锁外进行。

## 明确的使能与禁用请求

适配器设置 `SRNOS_InputFrame.request_enable=true`，快照记录递增的 `enable_sequence`、本机接收时间 `enable_ms` 和请求所属 `enable_epoch`。这是请求元数据，不是持续的 enabled 状态；普通通道、按键和相对量不会生成、刷新或重发请求。请求本身也不更新这些输入字段的时间戳。带失效 flags 的 frame 不能同时请求使能。

`request_disable=true` 独立增加 `disable_sequence`，记录 `disable_ms/disable_epoch`，并清除此前的 `enable_epoch`；通道、按键、相对量和它们的时间保持不变。请求不能同时含 enable 与 disable，也不能和失效 flags 混合。DISABLE 后接 CHANNELS 或 ENABLE 不会覆盖 disable 计数，消费者可检测同一周期内的停止；若同周期出现两者，应先停止并消费两者，再等待新的使能请求。失效/重新同步清除请求 epoch，保留计数。

消费者独立保存已见序号，检查来源在线、字段有效、新请求、接收年龄和 `enable_epoch==epoch` 后，再按应用安全条件决定是否接受。阻塞时也应消费请求，避免条件解除后误用旧请求。INVALIDATE 和重新同步清除 `enable_epoch`，保留计数用于去重；快照读取不会主动消费请求。恢复连接后仅补发 CHANNELS 不能恢复旧意图。一次快照间的多个请求可合并为最新一次，不能当作完整审计队列。

公共 Frame/Snapshot 结构已扩展，SDK 与父项目须整体重编译。使能接受条件、超时和执行器行为完全属于应用。

## SirinRMT v1

SirinRMT 是输入模拟协议。它发布通道、按键、相对增量、车体系平面速度及明确的使能/禁用请求，自定义业务数据使用 [Message](../Message/README.md) 的其它 service/type。旧 EJOY 名称、magic 和接收队列不作为兼容接口保留。

同一 30 字节载荷经 HostComm、专用 UART、UDP 或 VT03/VT13 图传传输时使用 Message service=`0x21`、type=`0x01`。旧 VT02/VT12 也可显式注册接收，直接用 `0x0302` 的完整 30 B payload，复用 `SRNOS_MESSAGE_VT02` 的 `0x22/1` 路由。所有多字节字段 little endian；保留/填充字节必须为零。

| 偏移 | 长度 | 内容 |
|---|---|---|
| 0 | 4 | ASCII `SRMT` |
| 4 | 1 | version=1 |
| 5 | 1 | kind：0 INVALIDATE、1 CHANNELS、2 BUTTONS、3 RELATIVE、4 ENABLE、5 DISABLE、6 PLANAR_VELOCITY |
| 6 | 4 | 非零 session |
| 10 | 4 | sequence，发送端在所有 kind 间共享递增序号 |
| 14 | 16 | 下表定义的 body |

| kind | body |
|---|---|
| CHANNELS | first:u8、count:u8、values:count×i16，之后补零；first 从 0 开始，count 1…7，first+count≤16，value -10000…10000。16 通道分三包发送，各通道独立过期。 |
| BUTTONS | valid:u64、held:u64；held 必须是 valid 的子集。每包替换完整按键组。 |
| RELATIVE | mask:u8、零×3、dx:i32、dy:i32、dz:i32；mask 1…7，未选中轴 delta 必须为零。 |
| PLANAR_VELOCITY | body 0/4/8 为三个有限 IEEE754 float32（vx/vy/wz），body 12…15 必须为零；无归一化或死区。 |
| INVALIDATE | 零×16，来源标记 DISABLED。 |
| ENABLE | 零×16，发布一次 request_enable；不更新通道、按键或相对量。 |
| DISABLE | 零×16，发布一次 request_disable，取消此前使能请求；保留输入有效性和各字段时间。 |

每个传输实例独立保存 session、序号、peer 和来源。当前发送端有效期间拒绝重复/乱序包、不同 session 或其它 UDP peer；超时后允许新发送端取得来源，先使旧字段失效。序号支持 uint32 回绕，前向差必须小于 2^31。INVALIDATE、ENABLE 和 DISABLE 同样消耗序号。它是去重与发送端占用机制，不是认证或永久防重放；接收 ENABLE 不代表已通过应用安全门。

启用 `remote.sirinrmt_transports` 后自动注册 `sirinrmt.host` / `.uart` / `.udp` / `.video`，也可启动期手动调用 SirinRMTRegister，避免同时重复注册同一传输来源。超时使用 remote.offline_ms，默认 300 ms。received_ms 是本机接收时刻，不能测量未同步的发送端到接收端传输年龄。发送方需周期刷新每个需要保持有效的通道和按键组；其它组有新包不会延长旧字段有效期。无按键上升沿的初次 held 状态需要应用明确处理。

VT02/VT12 由父项目在启动期显式调用 `SRNOS_SirinRMTRegister("sirinrmt.vt02", SRNOS_MESSAGE_VT02, port, timeout_ms, &source)`，其中 `port` 为已配置的 `remote.vt02_port`，不可为零。总计最多四个接收实例，VT02 同样占用一个实例与 Input 来源；不新增轮询线程、报文队列或串口所有者。A5 包号只用于外层诊断，接收器检查 SRMT 内层 session/sequence。串口重置会使该来源失效，保留已见 session/sequence 的去重状态；键鼠来源独立。通道与使能策略由父项目定义。

C 编解码器不读写硬件。Python 编码器和限时模拟器位于 `Tools/SirinLink/sirin_rmt.py`，用法见 [SirinLink 工具](../../../Tools/SirinLink/README.md)。接收回调在相应传输任务内执行；每实例只能由对应的单一传输生产者调用。

## 互斥运动目标

`InputDescriptor.planar_velocity=true` 声明支持；SirinRMT 注册的来源自动声明。`InputFrame.update_planar_velocity` 与 channel_mask 互斥，三分量全部有限才发布。速度单独过期，ENABLE、按键和相对增量不能续期；SDK 不施加机器人速度上限。应用须声明车体系参考点，世界系转换由发送端负责。

`motion_kind` 区分 NONE/CHANNELS/PLANAR_VELOCITY。通道与速度类型切换时清除两类旧有效位及旧 enable_epoch，并增加 motion_epoch；同一消费者周期内往返切换仍可检测。DISABLE 请求身份保持，不能被类型切换覆盖。应用同时观察来源 epoch 和 motion_epoch，切换时撤权并重新建流/使能；仅看到新鲜值不代表执行器获准运动。失效/重新同步清除运动有效性，保留计数用于观察。
