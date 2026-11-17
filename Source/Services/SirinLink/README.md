# SirinLink

SirinLink 提供 CDC1 上的命令、参数访问、控制邮箱与遥测；CDC0 继续用于日志。COBS 分帧、SRN-Link V2 字段和 CRC32 完全沿用当前线协议，C/C++ 接口统一使用 `SRNOS_`。父项目只包含 `SirinEmbedOS/Services/SirinLink/SirinLink.h` 和所需 SDK 公共头，不需要 FreeRTOS、CMSIS 或 HAL 头。

## 父项目注册

在 `SRNOS_ApplicationConfigure()` 设置 `sirin_link_setup` 和 `sirin_link_user`。回调时机见[启动注册与容量](#启动注册与容量)；setup 返回失败会使启动配置失败，不会带着部分成功的注册继续运行。

```c
#include "SirinEmbedOS/Kernel/System.h"
#include "SirinEmbedOS/Services/SirinLink/SirinLink.h"

static uint16_t ReadVersion(void *user, const uint8_t *args, size_t args_len,
                            uint8_t *response, size_t capacity, size_t *length)
{
    (void)user; (void)args; *length = 0;
    if (args_len != 0) return SRNOS_SIRIN_LINK_STATUS_BAD_LENGTH;
    if (capacity < 1) return SRNOS_SIRIN_LINK_STATUS_INTERNAL_ERROR;
    response[0] = 1;
    *length = 1;
    return SRNOS_SIRIN_LINK_STATUS_OK;
}

static SRNOS_SirinLinkResult RegisterApp(SRNOS_SirinLinkRegistration *registration,
                                       void *user)
{
    (void)registration;
    return SRNOS_SirinLinkRegisterExec(0x0100, ReadVersion, user);
}

void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config)
{
    config->sirin_link_enabled = true;
    config->sirin_link_setup = RegisterApp;
    config->sirin_link_user = NULL;
}
```

`SRNOS_SirinLinkRegistration *` 是只在 setup 调用期间有效的借用上下文。父项目不能构造、解引用或保留它用于运行期注册。自定义消息使用 `SRNOS_SirinLinkRegisterHandler(registration, service, message, kind_mask, qos, callback, user)`，主动事件先使用 `SRNOS_SirinLinkRegisterTxTopic()` 声明。重复消息方向、重复发送主题和覆盖内置 SYSTEM/CONTROL 路由均被拒绝；启动完成后所有注册冻结。

| 能力 | 注册/使用接口 | 执行上下文 |
|---|---|---|
| 命令 | `SRNOS_SirinLinkRegisterExec()` | HostCmd 执行业务，HostComm 发送最终响应 |
| 自定义消息 | `SRNOS_SirinLinkRegisterHandler()` | HostComm；耗时工作用 `SRNOS_SirinLinkSubmitDeferred()` |
| 二进制遥测 | `SRNOS_SirinLinkRegisterTelemetryEx()` / `PublishTelemetry()` | 配置、校验、poll、完成通知均在 HostComm |
| 标量源 | `SRNOS_TelemetryRegister()` | HostComm 根据订阅频率读取 |
| 控制邮箱 | `SRNOS_SirinLinkRegisterControl()` / `TakeControl()` | HostComm 接收，应用任务读取副本 |
| 主动事件 | `SRNOS_SirinLinkRegisterTxTopic()` / `PublishEvent()` | 任务上下文提交，HostComm 发送 |

业务请求、应答和遥测采用字节数组与明确长度；不要把本机 C 结构体直接当成线格式。回调的请求/应答指针仅在本次调用内有效，延后请求会复制入固定工作队列。注册时的 `user`、标量名称/单位、控制邮箱存储须保持至服务结束；只读变量名称/单位由独立目录复制。除 Exec/Deferred 在 HostCmd 执行外，回调都必须有界、非阻塞；回调不持有 SirinLink 临界区。公开运行期接口限任务上下文。 `SRNOS_SirinLinkSubmitDeferred()` 只允许当前 HostComm 请求处理器提交自己的 service/message/sequence，每个请求最多接受一次；其他任务、伪造请求身份和重复提交均返回 BUSY。成功提交后 SDK 保证走延后完成路径。

## 启动注册与容量

本节统一说明 SirinLink、只读 Telemetry 及相关消息路由的启动注册顺序与固定容量。全部配置和注册冻结均发生在 `osKernelInitialize()` 之前；配置回调只能声明配置或注册，不得阻塞 I/O 或创建线程。

1. 静态 C++ 对象可先登记 Telemetry/Parameters；随后调用 `SRNOS_ApplicationConfigure()`，声明系统配置及应用注册。
2. SDK 装配 Message、遥控和裁判等协议，再调用父项目 `bind_channels`。此时可继续登记 Telemetry、绑定 VideoLink，或声明 Input/Message 路由。
3. SDK 冻结 Input 和 Message 注册，之后不能在 `sirin_link_setup` 中补登这些路由。
4. 启用 SirinLink 时，SDK 登记内置协议、参数服务、启用的遥测和 Message HOST 路由，再调用父项目 `sirin_link_setup`；返回后冻结 Telemetry 目录与 SirinLink 注册。
5. Kernel 完成后续配置，确保 Telemetry 已冻结（关闭 SirinLink 时也执行），再冻结 Parameters；完成其他资源配置后冻结系统配置。

Telemetry 可在上述任一尚未冻结的登记阶段注册。SirinLink 关闭时仍保留只读缓存；订阅和发布均不能改变已经冻结的描述或路由。

| 对象 | 固定容量或预算 |
|---|---|
| Exec / 二进制遥测流 / 控制邮箱 | 8 / 12 / 8 项 |
| 独立只读 Telemetry 目录 / SOURCE 频率组 | 各 32 项 |
| 消息 handler | 共 40 项；内置服务最多占 22 项，通用 Message 最多 12 项及父项目 handler 均计入总额 |
| 发送主题 | 16 项，独立于 handler 容量 |
| 发送槽 | 共 12 项，FIFO 最多占 8 项；完成通知复用原槽 |
| 活动请求 | 10 项，对应工作队列 4 + 执行中 1 + 完成队列 4 + HostComm 待发送完成 1 |
| 已完成响应重放 | 4 项，TTL 2 秒 |
| 服务线程栈 | `SRNOS_HostComm` 3 KiB，`SRNOS_HostCmd` 2 KiB |
| 启用 SirinLink 的保守启动 heap 预算 | 9 KiB，由私有预算函数按实际栈常量及有界队列预留汇总 |

注册超限、重复 ID 或冲突会拒绝启动。增加注册不创建线程，不改变既有优先级与启动屏障。订阅代次、注册表和缓存属于静态存储，不增加运行期分配；具体结构大小、对齐与最终 D1 用量以对应固件的 map 为准。

## 请求与完成

活动请求表与已完成响应重放缓存彼此独立，容量见[启动注册与容量](#启动注册与容量)。活动请求在结束或会话清理前不被 TTL 淘汰；同一服务/消息/序号的相同请求返回 ACCEPTED，不重复执行，正文不同返回 CONFLICT。完成重放缓存不是无限期的业务去重存储，业务需要更长去重窗口时应自行设计幂等命令。

HostComm 将已出队但暂时无法发送的完成结果保存在自己的固定槽，直到发送队列接收或会话失效；不会放回可能被生产者填满的完成队列。即时处理的响应也先保存到完成重放缓存，防止发送拥塞直接丢失结果。无资源时返回 BUSY，不扩大队列、不动态分配。

## 等待、发送与干扰控制

HostComm 按 RX/TX/工作完成通知及最近截止时间等待。未订阅且没有发送待重试时，仅按诊断快照截止点唤醒，默认 100 ms。每轮最多读取 1024 字节 RX；耗尽预算后阻塞至少一个 tick，再继续处理，保留 COBS 流解码状态。

遥测 `poll_interval_ms` 指定 HostComm 的轮询间隔，0 表示按订阅周期向上取整到毫秒，未指定周期时用 1 ms。内置 IMU/INS 有订阅时使用 1 ms，以保留最老样本 4 ms 刷新语义；SystemMonitor 使用 1 s。延迟只处理一轮，再选择未来截止点，不补跑轮询。订阅不改变底层传感器或监测采样频率。

LATEST 合并同一流的旧待发值；FIFO 不覆盖，满时返回 `SRNOS_SIRIN_LINK_ERR_FULL`。发送槽及 FIFO 上限见[启动注册与容量](#启动注册与容量)。FIFO 的可选完成回调恰好执行一次，区分完成、失败、取消，统一由 HostComm 在锁外调用；对应槽在通知被取走前保持占用，因此不会丢失完成事件。成功提交只表示数据已复制入队，USB 完成不代表主机应用已经读取。

`PublishTelemetry` 可以由应用任务调用，返回后可复用样本缓冲。调用开始时尚未订阅返回 `ERR_PARAM`；编码期间遇到取消、重新订阅或断连则返回 `ERR_BUSY`，不产生完成回调。每次成功修改订阅都更新私有代次，预留发送槽和提交编码结果时分别核对；取消后重新启用不会接收旧调用的编码结果。已成功提交的 FIFO 记录仍可能随后收到一次 CANCELLED。正在编码或已交给 USB 的槽都由当前使用者释放，取消不会提前复用其内存；编码与应用回调始终在临界区外。

IMU/INS 入队与批量读取均以单条记录为保护单位；编码、遍历和批处理在临界区外。断连清理的重放缓存清零也在临界区外；在途 USB 数据仍等真正完成或端点确认取消后释放。DTR 会话变化清理订阅与活动请求，旧会话的完成结果不发到新会话。

## 接入边界与诊断

父项目使用 `SirinLink.h` 中的 `SRNOS_SirinLink*` 类型和函数，通过不透明注册上下文声明自定义消息。transport、线程入口及底层链接状态是 SDK 私有实现；纯 COBS/协议编解码头可独立用于主机编解码。接口不提供旧命名兼容头或别名。

## 只读变量与 SOURCE V2

`Services/Telemetry.h/.hpp` 提供静态 C++ `SRNOS::Telemetry::Value<T>{name,unit,maxHz}` 和 C 描述登记，四种标量在业务任务中通过 `Publish(value, sample_time_us, valid)` 更新 SDK 缓存。不支持任意 `volatile void *`/getter 作为遥测源。登记阶段与冻结顺序见[启动注册与容量](#启动注册与容量)。

SOURCE V1 保留 service4/message03～07格式和5～100 Hz、500标量每秒限额。V2使用0x10～0x14，频率uint16、5～1000 Hz步进5，且不超过目录maxHz；V1+V2总额不超过8000标量每秒。一个变量只保留一个版本订阅，后者替换前者；失败批次不改变任何订阅。实际(version,rate)组按固定目录半区每包最多16项，以组槽/半区作为LATEST key，避免不同频率互相合并。真实sampleTime、sequence、valid来自业务发布缓存，encodeTime只表示HostComm编码时间；断连不阻塞Publish。详细字节布局见 [SOURCE V2协议](../../../Docs/SirinLink.md#只读-source-v2)。

## IMU 静态校准命令

CONFIG `0x09` 将手动静止校准交给已有 HostCmd，`0x0A` 在 HostComm 读取进度。正文 `01 03` 选择双 IMU，约 5 s 采集，仅估计陀螺零偏；EEPROM 读回验证后返回成功，异常返回错误及部分保存掩码。总执行期限 20 s，不含排队；HostCmd 期间不能处理其他耗时任务，HostComm 状态查询/遥测继续。新值下次启动生效，断连不取消已执行工作。校准路由计入内置服务容量，无独立线程、队列或栈预算。协议、去重期限和错误编码见 [线协议](../../../Docs/SirinLink.md#手动静态-imu-校准)。

完整外层字段、编码样例及全部服务/消息/流 ID 见 [COBS 与协议总表](../../../Docs/SirinLink.md#cobs-帧与校验)；主机存储交互见 [EEPROM 设计](../../../Docs/Storage.md)。

## 通用消息与输入模拟

启用 `messages.host_enabled` 后，SDK 将 [Message](../Message/README.md) 的 HOST 路由注册为 V2 EVENT handler 和对应 TX topic；登记时机及共享容量见[启动注册与容量](#启动注册与容量)。System/Control 保留服务不能用作通用 HOST 路由。`remote.sirinrmt_transports` 的 HOST 位提供 service 0x21/type1 的 [SirinRMT](../Input/README.md) 输入模拟；控制映射仍归应用。既有 SirinLink request/response 与遥测接口不变。Codec 的 ProtocolPack 返回带首尾零的 COBS 帧；新增 ProtocolPackRaw 返回 header/payload/CRC32，供 UDP 和图传 packet 封装。
