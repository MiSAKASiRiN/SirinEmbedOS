# VT02/VT12 图传 UART

独立图传链路 UART 收发，公开头 `SirinEmbedOS/Services/RemoteControl/VT02/VT02.h`。串口为 **115200/8N1**、普通不反相 RX，可显式启用 TX；不是电源管理模块 User 串口的裁判常规链路，也不是 VT03/VT13 的 921600/21 B 遥控协议。接口名称随 MCU 直接连接的 VT02，覆盖 VT02/VT12 设备对；RX 指选手端到 MCU，TX 指 MCU 到选手端。完整接入步骤与可编译示例见 [双向接入指南](../../../../Docs/VT02VT12.md)。

## 官方依据与范围

采用 [官方 2024 串口协议附录 V1.6.4（20240715）](https://terra-1-g.djicdn.com/b2a076471c6c4b72b574a977334d3e05/RM2024/RoboMaster%20%E8%A3%81%E5%88%A4%E7%B3%BB%E7%BB%9F%E4%B8%B2%E5%8F%A3%E5%8D%8F%E8%AE%AE%E9%99%84%E5%BD%95%20V1.6.4%EF%BC%8820240715%EF%BC%89.pdf)：第 1 节串口/A5 格式，第 4 节**图传链路**（表 4-1、4-2），附录一 CRC 示例。官方入口为 [2025 规范文件公告](https://www.robomaster.com/zh-CN/resource/pages/announcement/1768)，其中保留旧版下载；[VT02 产品页](https://www.robomaster.com/zh-CN/products/components/detail/2611) 提供 VT02&12 用户指南。协议依据固定于旧版，不采用 2026 新图传格式。

| 接收命令 | 数据 | SDK 出口 |
|---|---|---|
| `0x0304` | 12 B 键鼠 | `vt02` Input，键盘位 0..15，鼠标左右位 16/17，三轴原生有符号样本逐帧累加为 relative_total；无模拟通道、拨轮通道或档位。 |
| `0x0302` | 30 B 自定义控制器载荷 | Message transport=`SRNOS_MESSAGE_VT02`，service/type=`SRNOS_VT02_CUSTOM_SERVICE`/`SRNOS_VT02_CUSTOM_MESSAGE`（0x22/1），endpoint=绑定 Serial port；原样复制全部 30 B。 |

A5 帧含 5 B 头、2 B 小端命令、载荷、2 B CRC16；复用现有 A5 CRC/Endian helper。键鼠保留位供独立解码接口检查，不强制为零；左右鼠标值须为 0/1。自定义数据不要求 SM/SRMT 格式、不转换为控制输入、不刷新键鼠时效。Message sequence 为 A5 的 8 位包号，可能回绕，不附加去重或可靠重传语义。

父项目可显式在 `SRNOS_MESSAGE_VT02` 和绑定端口上注册 [SirinRMT](../../Input/README.md#sirinrmt-v1) 订阅者，直接解码 30 B 载荷，使用内层会话/序号。解码器的 `0302` 原样投递合同不变；串口流重置也使已注册的 SirinRMT 来源失效。

本模块只将上述旧图传接收命令发布为 Input/Message；未知 CRC 有效命令跳过。内置绑定可在 Serial 启动配置显式启用 `tx_enabled` 后调用 `SRNOS_VT02Send(command, payload, length, sequence, timeout_ms)`，将0～300字节载荷封装为完整A5帧并复制到既有Serial队列；`SRNOS_VT02Flush(timeout_ms)` 等待UART TC。两者均为任务上下文，TX未启用或内置来源未启动时返回STATE，TX完成不代表图传/选手端已接收。无自动发送、重试、信道配置、激活或配对功能；具体命令能否上行取决于发送端固件，不能从通用A5封装推断所有年份均支持。硬件及选手端仍需正确连接和配置。

Send使用309字节局部A5缓冲，并调用Serial的复制发送接口；调用任务应预算嵌套栈开销。它不创建线程或增加静态队列，参数非法时不发送；超时或后续Flush失败不撤销已经入队/发送的字节。只对返回状态做自动重试可能重复投递，可靠性与幂等策略由应用定义。

## 内置绑定

在 ApplicationConfigure 设置 Serial 和 `remote.vt02_port`，其余配置沿用 SDK 默认值。以下是配置回调内的 C 片段；`route` 是静态保存的 `SRNOS_MessageRoute`，必须检查注册结果：

```c
#include <SirinEmbedOS/Services/RemoteControl/VT02/VT02.h>
#include <SirinEmbedOS/Services/Message.h>

config->serial[0] = (SRNOS_SerialConfig){
    .port=SRNOS_SERIAL_UART9, .baud_rate=115200, .data_bits=8, .stop_bits=1,
    .parity=SRNOS_SERIAL_PARITY_NONE, .mode=SRNOS_SERIAL_DUPLEX,
    .rx_enabled=true, .tx_enabled=true /* 仅 RX 时可设 false */
};
config->serial_count = 1;
config->remote.vt02_port = SRNOS_SERIAL_UART9;
config->remote.vt03_port = SRNOS_SERIAL_NONE;
const SRNOS_MessageRouteConfig r = {
    .transports=SRNOS_MESSAGE_VT02, .endpoint=SRNOS_SERIAL_UART9,
    .service=SRNOS_VT02_CUSTOM_SERVICE, .type=SRNOS_VT02_CUSTOM_MESSAGE,
    .max_payload=30, .max_age_ms=1000, .depth=1, .policy=SRNOS_MESSAGE_LATEST
};
/* SRNOS_MessageRegister(&r, &route) 必须返回 SRNOS_MESSAGE_OK。 */
```

任务中用 `InputFind("vt02")`/`InputReadSnapshot` 读取键鼠，用 `MessageReceive(route, &info, bytes, sizeof(bytes))` 消费自定义消息；检查返回值、有效位和年龄。需要保留每次自定义事件时选 FIFO 并核对容量；LATEST 仅保留最新值。队列内存计入既有 MessageHeapBudget。MessageSend 不支持 VT02 transport；显式原始A5测试发送使用上述专用入口，不使用VT03 VideoLink或第二个Serial所有者。

内置绑定复用优先级 47、3072 B 栈的 InputIO，不新建线程。静态实例在当前 ARM ELF 中共 404 B，包含 309 B A5 缓冲和诊断；每轮最多读取 1024 B，固定有界工作。只有 0304 刷新 `vt02`，超时由 `remote.offline_ms`（默认 300 ms）和 Input 字段时效判断；应用自行决定更严格期限。

## 独立接口与错误恢复

未使用内置绑定时，可零初始化 `SRNOS_VT02`，在 bind_channels 内调用 Init 独占 Serial；Init 后设置 framer 的 frame_callback/custom_callback/user，再由一个任务 Poll。不要同时占用同一端口。纯 FramerFeed 可脱离串口测试，每个键鼠/自定义帧都同步回调；回调数据仅在调用期间借用，不得阻塞或重新进入 Feed/Poll。

UART 错误、溢出、恢复及读失败清空排队/半帧；半帧超过 30 ms 未读到新字节时清空并记录 stream_resets。内置来源遇流重置失效，后续有效键鼠帧恢复。CRC16 失败的已识别包整体丢弃，不在载荷中寻找嵌套命令。缓存上限允许 300 B 载荷以完整跳过未知 A5 包；超长/坏头滑动重同步。Framer 诊断计数仅由其轮询任务访问；Message 投递/覆盖/过期/丢弃由每路 MessageGetStats 查询。

验证范围见 [CHANGELOG](CHANGELOG.md)。
