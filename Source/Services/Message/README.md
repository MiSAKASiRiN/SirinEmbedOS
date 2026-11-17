# 通用 Message 分发

`Services/Message.h` 提供有界、按 service/type 匹配的消息分发。消息与 Input 独立：配置、传感器附加数据、调试请求和其他业务数据均可使用消息，不必伪装成遥控通道。

启动期注册 `SRNOS_MessageRouteConfig`，ApplicationConfigure 或 bind_channels 均可；Kernel 在所有应用绑定后冻结，再向 HostComm 注册对应 EVENT handler/TX topic。相同 service/type 可以多次订阅，每个订阅独立接收。最多 12 个路由（HOST 还受 SirinLink 共用 handler/topic 注册表约束），队列总预算上限 8192 B，单消息最大 480 B。错误的容量、端口冲突和冻结后注册会被拒绝。

- FIFO：depth 1…16，满时丢弃新消息，适合不允许静默覆盖的命令/事件；发送成功不等于已执行，需要业务自己定义应答。
- LATEST：depth 必须为 1，新值覆盖旧值，适合状态发布。
- handler：depth=0，payload 只在回调内借用，不能保存指针；回调在传输任务中、SDK 锁外执行，必须有界且不阻塞、不分配、不做外设 I/O、不递归分发。不同传输可同时调用同一 handler，需要自己的同步；通常应用应选队列。

任务调用 MessageReceive 复制队列头部，EMPTY 表示暂无消息，BUFFER_SMALL 保留原消息。每队列只由一个任务消费，需要多个消费者各自注册路由。max_age_ms=0 不过期，非零时按本地 SRNOS_TimeMs 同时检查分发和消费时的年龄；过期消息不会返回业务。MessageGetStats 提供投递、丢弃、过期、覆盖、handler 错误和队列高水位。

MessageDispatch 尝试所有匹配路由；OK 表示至少一个接收，不能用它推断每个订阅都成功。没有接受方时，队列满返回 FULL；已匹配但年龄/长度/handler 拒绝返回 REJECTED；无匹配为 NO_ROUTE。payload 在调用返回后即可复用，队列保存副本。运行期任务 API 不允许 ISR。所有注册描述在启动时复制；handler user 指向的对象需持续有效。队列在 osKernelInitialize 后一次分配，运行期不分配。

## 传输与协议

| 入口 | 启动配置 | wire |
|---|---|---|
| HostComm CDC | messages.host_enabled=true，并启用 sirin_link/USB | 原 SirinLink V2：`00 + COBS(V2) + 00`，kind=EVENT，flags=0。既有 RPC/遥测继续走 SirinLink 自身服务。 |
| 专用 UART | messages.uart_port；对应 Serial 必须 RX/TX 全双工 8N1 | 同样的 V2 COBS 帧；SDK 独占端口。波特率由 Serial 配置。 |
| 以太网 UDP | messages.udp_port；启用 network/ethernet | 单 datagram 包含 raw V2：12 字节头、payload、CRC32；无 COBS、无末尾填充。此端口不得重复放入 network.udp_ports。 |
| 图传下行 | remote.video_link_enabled，VT03 UART 开启 RX/TX | A5/0311 的 30 字节 payload：SirinRMT 原生 SRMT 载荷，或下述 SM 紧凑消息。 |
| 旧 VT02/VT12 图传下行 | remote.vt02_port，115200/8N1 RX | A5/0302 原始30 B；transport=VT02，service/type=0x22/1，endpoint=Serial port。不要求 SM/SRMT，不支持 MessageSend。见 [VT02](../RemoteControl/VT02/README.md)。 |
| 本地 | 无传输配置 | 任务构造 MessageInfo，origin.transport=LOCAL，调用 Dispatch。 |

V2 的 header 布局为 magic A5、version 2、kind 2、flags 0、service:u8、type:u8、length:u16 LE、sequence:u32 LE。CRC32 复用 SirinLink 实现。`ProtocolPackRaw` 编码 packet transport；`ProtocolPack` 返回包含双侧零分隔符的 COBS stream frame。零长度 payload 合法。不支持此层分片或自动 RPC。

HOST 路由禁止 service 0/3（SirinLink 的 System/Control 特殊路径）。SDK 已有服务编号需要避开；SirinRMT 保留 0x21/1，旧 VT02 原始自定义载荷使用 0x22/1，应用示例使用 0x40/1。配置了 HOST 路由就注册同一键的发送 topic；与应用 sirin_link_setup 中同键冲突会拒绝启动，不覆盖已有 handler。

图传通用下行 SM：`'S','M',1,service,type,length,sequence:u32LE,payload[20]`；length 0…20，未用字节补零。SRVL 的目录/回显请求由 VideoLink 服务处理。上行 MessageSend 经 A5/0310 发送 raw V2 并补零到 300 B，payload 最大 284 B；接收端按 V2 length 截取帧后验 CRC。图传上下行长度不同，因此使用不同封装。

消息携带 origin.transport、endpoint、UDP peer IP/port、sequence 和 received_ms。endpoint 对 UART 是 Serial port ID，对 UDP 是本地监听端口，VT02 是 Serial port ID，其余为零；注册时 endpoint=0 表示该传输的任意 endpoint。来源信息用于路由/业务应答，不代表身份认证。

MessageSend 复制提交，OK 只代表本机队列接受，不代表对端收到或执行。UART/UDP 发送目的 endpoint 必须等于配置端口，UDP 还需填写 peer；Host 需要已注册 TX topic。UDP 发送沿用 Network 的约1.5 KiB临时 datagram，加上 Message 编码帧和调用链，发送任务建议至少4 KiB栈并实测高水位；不可从只有2 KiB栈的任务直接照搬发送示例。VideoLink 的遥测与通用发送共享 40 Hz 上限，拥塞立即返回错误。应用应检查返回值，按数据语义决定重试/丢弃。

## 调度与资源

UART parser 复用优先级 47、3072 B 栈的 InputIO，每轮最多读取 8×128 B；100 ms 不完整帧超时、超长帧、Serial 错误/溢出后丢弃到零边界重新同步。HostComm 与 Network 使用既有线程；UDP 专用监听复用 Network，每包直接分发，避开原始 UDP 的单槽控制选择器。四个 raw UDP 端口仍可用，专用消息端口是额外一个 PCB，使用现有 lwIP heap。队列不增加 worker；业务任务通过 Receive 消费。

完整示例见 [InputMessages](../../../Examples/InputMessages/README.md)，输入协议见 [Input/SirinRMT](../Input/README.md)。

UDP adapter 在启动时绑定 Network 的唯一私有系统 datagram 回调；Network 只交付已完成来源/长度检查的借用数据报，Message 负责 origin 和 V2 解码。绑定在 Network Prepare 时冻结，运行期不重绑；四个 raw UDP 端口及独立系统端口的容量保持。UART 的完整 8N1 全双工条件由同一个私有校验函数供 SystemValidate 与 Prepare 使用，校验通过不代替后续资源占用检查。
