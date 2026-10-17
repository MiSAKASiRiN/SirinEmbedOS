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

SDK 不注册默认业务 UDP 服务。应用自行消费/发送；父项目 NetworkBringUp 的
5000 端口回显仅是诊断程序，正常底盘固件没有回显任务。无网关时只支持同子网通信。
调用 Send/Receive 的应用任务需为其约 1.5 KiB 局部复制留出栈空间。

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
服务额外保守预算 7 KiB RTOS heap，协议栈使用独立 8 KiB 静态 D1 heap，
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

## 验证

`Tests/Services/Network/run_host_tests.sh` 编译真实 lwIP 和服务，以伪造二层/内核
驱动测试默认及自定义地址、ARP、0..1472 字节 Ping/UDP、IP/UDP/ICMP 校验和、
分片/禁用协议/外部地址拒绝、队列容量、ARP 过期、link epoch 以及显式线程等待。
可选接收策略回归覆盖保留/替换/锁存、百包突发、重复事件合并、超时及断线保持、
取快照后消费者抢占的两种交错、API 上下文和计数。父项目另用真实 lwIP、此 SDK
服务及真实底盘任务/CAN 回调验证 STOP 到零输出的完整软件路径。
启用 ASan/UBSan 且 UBSan 报错即失败。`Tests/Serial` 覆盖 SDK 默认值和配置依赖，
Ethernet 回归覆盖原始接口所有权及发送 epoch 检查。板端证据位于父项目
`Docs/Validation/2026-10-14-IPv4`；短测不代表线速、攻击流量或全部网关场景验收。
