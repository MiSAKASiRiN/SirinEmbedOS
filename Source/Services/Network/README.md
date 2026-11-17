# Network：静态 IPv4 / UDP / Ping

SDK 自维护单一 Network worker，通过已建立的 Ethernet API 使用 LAN8720。
lwIP 2.2.1 固定版本与原始许可证位于 `ThirdParty/LwIP`，适配/配置在本目录。
上游来源及 SHA-256 见该目录 README / SHA256SUMS；不修改上游源码。

## 启动和公共 API

`SRNOS_SystemConfigDefaults` 默认关闭网络，预置 `192.168.1.50/24`、网关
`0.0.0.0`、零 UDP 端口。应用在 `SRNOS_ApplicationConfigure` 中开启：

```cpp
config->ethernet.enabled = true;
config->network.enabled = true; // 使用 SDK 地址，自动回应单播 Ping
config->network.udp_ports[0] = 5000;
config->network.udp_port_count = 1;
// 可选覆盖；不覆盖就保持 192.168.1.50。
config->network.address[3] = 77;
```

地址、掩码、网关、最多四个 UDP 端口均在启动时验证、复制和冻结。不支持运行期
重配、DHCP、DNS、TCP、IPv6、组播/广播 UDP、广播 Ping、IP 转发、分片或重组。
只接受发往本机的 IPv4 UDP/ICMP，ARP 用于邻居发现。支持常规 `/1` 至 `/30`
子网，拒绝网络/广播/回环/组播地址；可选网关必须是本子网内另一单播主机。
ICMP Echo 由 worker 自动响应，不需要应用轮询；ICMP 错误处理使用 lwIP。

公共头是 `SirinEmbedOS/Services/Network.h`，不暴露 lwIP/RTOS 类型：

- `SRNOS_UDPSend(local_port, destination, port, payload, length)`：任务上下文、
  非阻塞复制，0..1472 字节；本地端口须预声明。返回 OK 仅表示进入 SDK 队列。
- `SRNOS_UDPReceive(local_port, &datagram)`：任务上下文、单端口单消费者，
  返回源 IP/端口、精确长度、采样毫秒和数据。默认每端口一个槽，满时丢新包；
  可通过启动冻结的 `udp_rx_select[i]` 为指定端口选择替换或事件锁存策略。
- `SRNOS_NetworkGetStatus`：配置、就绪/链路状态和计数。`udp_submitted` 表示
  协议栈已交给二层；MAC 完成须另外查看 `SRNOS_EthernetGetStatus`，UDP 没有送达保证。

SDK 不注册默认业务 UDP 服务。应用自行消费和发送数据，并定义业务协议。
无网关时只支持同子网通信。
调用 Send/Receive 的应用任务需为其约 1.5 KiB 局部复制留出栈空间。

## NTP 授时

可选 SNTPv4 客户端连接一个可信局域网 NTP 服务器（UDP 123），兼容 chrony。
服务器响应授时请求，客户端在网络就绪后主动请求；不依赖 ROS 消息，也不阻塞启动。

```cpp
config->ethernet.enabled = true;
config->network.enabled = true;
config->network.address[3] = 70;
config->network.ntp.enabled = true;
const uint8_t server[4] = {192, 168, 1, 50}; // 示例，父项目按实际配置
for (unsigned i=0; i<4; ++i) config->network.ntp.server_address[i]=server[i];
config->network.ntp.poll_interval_ms = 60000; // 0 表示首次成功后停止请求
```

启用时校验服务器是可路由的 IPv4 单播地址，且不是自身地址；周期为 0 或
15000..86400000 ms。配置在启动冻结，SDK 默认不指定服务器、不启用客户端。
客户端独占一个自动选择的临时 UDP 源端口，在应用端口全部绑定后建立，不占用四个
raw UDP 槽或 Message 端口；不能通过公共 UDPSend/Receive 访问该端口。

协议行为：

- 请求模式 3、版本 4；只接受配置的 IP/123、版本 3/4、模式 4、48 字节基本回复，
  并核对 originate timestamp。拒绝 LI=3、stratum>15、零时间、异常处理时长和迟到/重复包。
- 使用客户端单调收发时间与服务器接收/发送时间进行往返补偿；支持首次本地没有
  Unix 时间的交换。日期窗口为 2000-01-01 至 2099-12-31，覆盖 2036 NTP era 回绕。
- 收包期限 1 秒，扣除服务器处理时间后的 RTT 必须不超过 50 ms。软件时间戳包含
  二层排队与现有 5 ms worker 周期影响，不承诺亚毫秒对齐，不是完整 NTP 驯频实现。
- 等待 ARP 时每秒请求一次解析；不将已打时间戳的 NTP 负载交给 ARP 等待队列。
  失败后从 15 秒开始指数退避，最多 1024 秒；成功后按配置周期请求。
- 识别 KoD：RATE 至少等待 64 秒并尊重上限 1024 秒内的服务器 poll，DENY/RSTR
  停止本次启动的请求。链路变化取消待收请求，旧回复不能完成授时，RATE 退避不会因重连绕过。
- 首次成功前绝对时间无效；成功后失联继续本地计时并标记 holdover，详见
  [Clock](../Clock/README.md)。单次模式在成功后不因链路重连重新授时。

`NetworkStatus.ntp` 提供请求、成功、拒绝、超时、发送失败、KoD 计数及 pending/denied。
这是非认证 SNTP，限可信局域网；不支持 NTS、广播、DNS、多服务器选择或 NTP 扩展报文。
报文格式参考 [RFC 4330](https://datatracker.ietf.org/doc/html/rfc4330) 与
[RFC 5905](https://datatracker.ietf.org/doc/html/rfc5905)。既有 lwIP SNTP 示例尚有未完成
的响应校验，本客户端在 SDK 中实现，不修改或重新打包 ThirdParty。

NTP 完全运行于既有 Network worker，没有新线程、优先级或栈大小改动。仅增加
一个 UDP PCB（现有 lwIP heap）、48 字节局部报文及固定客户端/Clock 状态；
低频发送每轮最多一次，实际静态内存以最终 map 为准。

## 可选接收策略与事件锁存

`udp_rx_select[i]` 默认为 NULL，保持原有队列行为。非空时在 Network worker 中，
对通过协议栈和来源检查的数据报调用纯选择函数；传入新包及待收包的只读快照
（可能为 NULL，也可能在快照后已经被消费者取走）。回调不在临界区内执行，必须
有界、非阻塞，不分配、不打印、不调用网络 API，也不修改应用控制器状态。
SDK 不解释任何业务协议、STOP、速度或序号。

返回值为 `SRNOS_UDP_RX_DROP`（拒绝）、`KEEP_FIRST`（默认满时丢新）、
`LATEST`（替换未消费值）或 `LATCH`（锁存事件）；枚举均带 `SRNOS_UDP_RX_` 前缀。
未知返回值按 DROP 处理。LATCH 覆盖已有待收值，设置独立锁存标记，在消费者取走前
阻止普通值覆盖；重复 LATCH 合并为最新事件。它只适用于可合并、永久有效的幂等事件，
不能用于速度或其他可能过期的设定值。

锁存事件不因 100 ms 排队期限、断线或 link epoch 变化消失；链路断开时 Receive
也允许取走该事件，仍返回原始 `received_ms`。事件与标记在同一临界区内取走，
消费后才接受普通值；事件待消费期间到达的普通值直接丢弃，不会在消费后重放。
该保证从协议栈调用接收回调开始，不覆盖交换机、MAC、DMA、二层队列或 lwIP 内存
耗尽导致的上游丢包，也不是任务调度时延或停止电机的保证。

`udp_received` 统计合法来源的栈交付量，`udp_rx_dropped` 包含拒绝、满槽及锁存期间
丢弃；`udp_rx_replaced` 统计替换已有值，`udp_rx_latched` 统计锁存事件的接收次数
（包括合并）。配置及状态的布局以公共头为准，SDK 与使用方须使用同一版本整体编译。

## 选择器与单消费者示例

以下是独立示例协议：仅接受一个字节，`0x00` 表示可合并的停止事件，`0x01` 表示普通心跳。真实控制协议还应校验来源、版本、长度、数值和序号；不要把这个简化帧当成完整控制协议。

```c
#include <SirinEmbedOS/Kernel/System.h>
#include <SirinEmbedOS/Services/Network.h>

static SRNOS_UDPReceiveAction select_event(
    const SRNOS_UDPDatagram *incoming, const SRNOS_UDPDatagram *queued)
{
    (void)queued;
    if (incoming->length != 1u) return SRNOS_UDP_RX_DROP;
    if (incoming->data[0] == 0x00u) return SRNOS_UDP_RX_LATCH;
    if (incoming->data[0] == 0x01u) return SRNOS_UDP_RX_LATEST;
    return SRNOS_UDP_RX_DROP;
}

void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config)
{
    config->ethernet.enabled = true;
    config->network.enabled = true;
    config->network.udp_ports[0] = 5000;
    config->network.udp_rx_select[0] = select_event;
    config->network.udp_port_count = 1;
}

/* 只由一个应用任务调用；消费到的事件交给该任务的业务状态机处理。 */
bool take_stop_event(void)
{
    SRNOS_UDPDatagram datagram;
    if (SRNOS_UDPReceive(5000, &datagram) != SRNOS_NETWORK_OK) return false;
    return datagram.length == 1u && datagram.data[0] == 0x00u;
}
```

回调只决定入队策略，不更改控制状态；消费者收到停止事件后才修改自身状态机，重新使能的条件由应用定义。对锁存事件不再套普通运动值的年龄过滤，否则会在应用层丢掉已保留事件。`take_stop_event()` 中的完整数据报约 1.5 KiB，应纳入调用任务栈预算；普通心跳会被本次消费并返回 false。

## 所有权、调度与资源

`NO_SYS=1` 不意味着忙循环：所有 lwIP 调用（含 `sys_check_timeouts`）仅发生在
优先级 15、6 KiB 栈的 Network worker。每轮最多读取四帧、处理一条 UDP 发送，
随后显式等待 5 ms；Kernel 的 loop period 字段仅用于统计，不能替代等待。
服务私有预算函数复用创建线程的栈常量，额外保守预算保持 7 KiB RTOS heap，协议栈使用独立 8 KiB 静态 D1 heap，
不向应用堆动态申请内存。队列为共享 TX 两槽 + 一个 worker pending 槽、四端口各
一个 RX 槽；未配置端口不建立 queue/PCB，固定容量存储仍计入链接预算。
接收策略共用一个约 1.5 KiB 的 worker 静态快照缓冲，不额外增加每端口完整报文槽，
不增加线程栈；锁存标记与队列的修改由短临界区共同保护。
Network 与 lwIP 固定工作区占用静态 D1，具体字节数按最终 map 核对；RTOS worker 栈、lwIP 堆与静态报文槽分别核算，避免重复计算。

启用网络时独占 Ethernet 原始帧收发；公共原始 Send/Receive 返回
`SRNOS_ETHERNET_OWNED`，状态 API 仍可用。独立 raw Ethernet 模式保持可用。
MAC/PHY/DMA 仍由 Ethernet worker 唯一持有。向二层交付时核对预期 link epoch，
避免服务被抢占期间恢复链路后把旧报文重新标记为新 epoch。

UDP 的排队和等待 ARP 总寿命 100 ms；先确认邻居表再调用 `udp_sendto`，避免把
应用负载放入 lwIP 更长的 ARP 等待队列。满/过期/链路变化丢弃。交给 Ethernet 后
仍受二层自身 100 ms 队列期限约束，因此不是端到端 100 ms 交付承诺。UDP RX
普通值 100 ms 过期；所有 link epoch 变化（包括两轮间完成断开重连）清理普通
收发队列和 ARP，保留尚未消费的锁存事件。
ICMP 等栈自产报文可能使用 lwIP 默认的有限 ARP 等待；不承载应用控制命令。

## 通用消息端口

`SystemConfig.messages.udp_port` 可额外声明一个专用监听端口，不能与 network.udp_ports 重复。它接收 raw SirinLink V2 EVENT/CRC32 数据报，在 Network worker 内交给 [Message](../Message/README.md) 按服务/类型分发，绕过 raw UDP 的单槽/选择器，避免不同业务互相覆盖。四个 raw UDP 端口仍可同时使用；新增 PCB 使用现有 lwIP heap。该端口不能用 UDPReceive 取数据；MessageSend 的 origin.endpoint 为本地消息端口，peer 为对端地址/端口。应用回调不能从 Network 任务中阻塞或直接调用 lwIP，建议使用 Message 队列。

Network 只保存一个在 Prepare 前绑定、随后冻结的私有系统 datagram 回调，不包含 Message 私有头或解析其协议。Message 的 UDP adapter 将借用的数据报转换为 origin 并执行消息解码；回调在 Network worker、SDK 锁外执行，返回前不保留 payload 指针。该固定绑定不增加公开注册 API、PCB 数量、队列或数据报副本，拒绝结果沿用 udp_rx_dropped 统计。
