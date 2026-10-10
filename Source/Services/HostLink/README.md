# HostLink

2026-09-29 板测已验证参数事务、SOURCE V2、读取暂停恢复和重连；8路1kHz订阅实收约每路910条/s，不能宣称完整8000标量/s交付。原阈值静态误拒绝及上述遥测结果见 [初轮验收](../../../Docs/Validation/PersistenceBoard-20260929/README.md)；阈值调整后双IMU校准完成、活动重试去重及重启加载已 [复测通过](../../../Docs/Validation/StaticThreshold-20260929/README.md)。

HostLink 提供 CDC1 上的命令、参数访问、控制邮箱与遥测；CDC0 继续用于日志。COBS 分帧、SRN-Link V2 字段和 CRC32 完全沿用当前线协议，C/C++ 接口统一使用 `SRNOS_`。父项目只包含 `SirinEmbedOS/Services/HostLink/HostLink.h` 和所需 SDK 公共头，不需要 FreeRTOS、CMSIS 或 HAL 头。

## 父项目注册

在 `SRNOS_ApplicationConfigure()` 设置 `host_link_setup` 和 `host_link_user`。SDK 先注册内置协议、参数服务和启用的遥测，再调用父项目 setup；此时尚未启动调度器，只能声明注册，不应进行阻塞 I/O 或创建线程。setup 返回失败会使启动配置失败，不会带着部分成功的注册继续运行。

```c
#include "SirinEmbedOS/Kernel/System.h"
#include "SirinEmbedOS/Services/HostLink/HostLink.h"

static uint16_t ReadVersion(void *user, const uint8_t *args, size_t args_len,
                            uint8_t *response, size_t capacity, size_t *length)
{
    (void)user; (void)args; *length = 0;
    if (args_len != 0) return SRNOS_HOST_LINK_STATUS_BAD_LENGTH;
    if (capacity < 1) return SRNOS_HOST_LINK_STATUS_INTERNAL_ERROR;
    response[0] = 1;
    *length = 1;
    return SRNOS_HOST_LINK_STATUS_OK;
}

static SRNOS_HostLinkResult RegisterApp(SRNOS_HostLinkRegistration *registration,
                                       void *user)
{
    (void)registration;
    return SRNOS_HostLinkRegisterExec(0x0100, ReadVersion, user);
}

void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config)
{
    config->host_link_enabled = true;
    config->host_link_setup = RegisterApp;
    config->host_link_user = NULL;
}
```

`SRNOS_HostLinkRegistration *` 是只在 setup 调用期间有效的借用上下文。父项目不能构造、解引用或保留它用于运行期注册。自定义消息使用 `SRNOS_HostLinkRegisterHandler(registration, service, message, kind_mask, qos, callback, user)`，主动事件先使用 `SRNOS_HostLinkRegisterTxTopic()` 声明。重复消息方向、重复发送主题和覆盖内置 SYSTEM/CONTROL 路由均被拒绝；启动完成后所有注册冻结。

| 能力 | 注册/使用接口 | 执行上下文 |
|---|---|---|
| 命令 | `SRNOS_HostLinkRegisterExec()` | HostCmd 执行业务，HostComm 发送最终响应 |
| 自定义消息 | `SRNOS_HostLinkRegisterHandler()` | HostComm；耗时工作用 `SRNOS_HostLinkSubmitDeferred()` |
| 二进制遥测 | `SRNOS_HostLinkRegisterTelemetryEx()` / `PublishTelemetry()` | 配置、校验、poll、完成通知均在 HostComm |
| 标量源 | `SRNOS_TelemetryRegister()` | HostComm 根据订阅频率读取 |
| 控制邮箱 | `SRNOS_HostLinkRegisterControl()` / `TakeControl()` | HostComm 接收，应用任务读取副本 |
| 主动事件 | `SRNOS_HostLinkRegisterTxTopic()` / `PublishEvent()` | 任务上下文提交，HostComm 发送 |

业务请求、应答和遥测采用字节数组与明确长度；不要把本机 C 结构体直接当成线格式。回调的请求/应答指针仅在本次调用内有效，延后请求会复制入固定工作队列。注册时的 `user`、标量名称/单位、控制邮箱存储须保持至服务结束；只读变量名称/单位由独立目录复制。除 Exec/Deferred 在 HostCmd 执行外，回调都必须有界、非阻塞；回调不持有 HostLink 临界区。公开运行期接口限任务上下文。 `SRNOS_HostLinkSubmitDeferred()` 只允许当前 HostComm 请求处理器提交自己的 service/message/sequence，每个请求最多接受一次；其他任务、伪造请求身份和重复提交均返回 BUSY。成功提交后 SDK 保证走延后完成路径。

## 请求与容量

注册容量为 Exec 8、二进制流 12、独立只读标量目录 32、控制邮箱 8；自定义及内置消息共用 24 个路由槽、16 个发送主题槽。新注册不会创建线程；固定 `SRNOS_HostComm` 用 3 KiB、`SRNOS_HostCmd` 用 2 KiB 栈，原优先级与启动屏障保留。

异步请求使用独立的 10 项活动表：工作队列 4、执行中 1、完成队列 4、HostComm 待发送完成 1。活动请求在结束或会话清理前不被 TTL 淘汰；同一服务/消息/序号的相同请求返回 ACCEPTED，不重复执行，正文不同返回 CONFLICT。已完成响应使用另一个 4 项、2 秒的重放缓存；它不是无限期的业务去重存储，业务需要更长去重窗口时应自行设计幂等命令。

HostComm 将已出队但暂时无法发送的完成结果保存在自己的固定槽，直到发送队列接收或会话失效；不会放回可能被生产者填满的完成队列。即时处理的响应也先保存到完成重放缓存，防止发送拥塞直接丢失结果。无资源时返回 BUSY，不扩大队列、不动态分配。

## 等待、发送与干扰控制

HostComm 按 RX/TX/工作完成通知及最近截止时间等待。未订阅且没有发送待重试时，仅按诊断快照截止点唤醒，默认 100 ms。每轮最多读取 1024 字节 RX；耗尽预算后阻塞至少一个 tick，再继续处理，保留 COBS 流解码状态。

遥测 `poll_interval_ms` 指定 HostComm 的轮询间隔，0 表示按订阅周期向上取整到毫秒，未指定周期时用 1 ms。内置 IMU/INS 有订阅时使用 1 ms，以保留最老样本 4 ms 刷新语义；SystemMonitor 使用 1 s。延迟只处理一轮，再选择未来截止点，不补跑轮询。订阅不改变底层传感器或监测采样频率。

12 个发送槽中 FIFO 最多使用 8 个。LATEST 合并同一流的旧待发值；FIFO 不覆盖，满时返回 `SRNOS_HOST_LINK_ERR_FULL`。FIFO 的可选完成回调恰好执行一次，区分完成、失败、取消，统一由 HostComm 在锁外调用；对应槽在通知被取走前保持占用，因此不会丢失完成事件。成功提交只表示数据已复制入队，USB 完成不代表主机应用已经读取。

IMU/INS 入队与批量读取均以单条记录为保护单位；编码、遍历和批处理在临界区外。断连清理的重放缓存清零也在临界区外；在途 USB 数据仍等真正完成或端点确认取消后释放。DTR 会话变化清理订阅与活动请求，旧会话的完成结果不发到新会话。

## 验证与迁移

不保留旧命名兼容头或别名。父项目将旧 HostComm 入口迁至 `HostLink.h`、使用 `SRNOS_HostLink*` 类型/函数，并把直接访问底层 link 的注册改为上述不透明上下文接口。transport、线程入口及链接状态已移入 SDK 私有头；纯 COBS/协议编解码头仍可独立使用。

主机测试覆盖协议黄金向量、断连/背压、注册冻结与冲突、活动表寿命、重放、完成队列竞争、RX 预算和回调锁边界。内置遥测 C/Python 黄金向量未变。相关公共头可仅用 SDK Include/Config 以 C11 编译。这里的主机结果不代表本轮固件已经烧录或完成板端性能验收。

线协议及监视工具见 [Docs/HostLink.md](../../../Docs/HostLink.md)。当前等待与队列固件已完成 CDC 停读 12 秒、DTR 断连 5 秒与重连，实际 LATEST 合并 24 项、队列峰值 11、解码错误为 0；见 [SRNOS 板测](../../../Docs/Validation/SRNOSBoard-20260928/README.md)。持续 RX、长业务命令、物理 USB 拔插和真实 CAN 组合负载仍待验证。

## 只读变量与 SOURCE V2

新的 `Services/Telemetry.h/.hpp` 提供静态 C++ `SRNOS::Telemetry::Value<T>{name,unit,maxHz}` 和 C 描述登记，四种标量在业务任务中通过 `Publish(value, sample_time_us, valid)` 更新 SDK 缓存。旧 `SRNOS_HostLinkRegisterTelemetrySource` 及 `volatile void *`/getter 注册合同已移除。无需先初始化 HostLink；可在全局构造、ApplicationConfigure 或 host_link_setup 登记，最终在 setup 完成后统一冻结；关闭 HostLink 仍可发布到缓存。

SOURCE V1 保留 service4/message03～07格式和5～100 Hz、500标量每秒限额。V2使用0x10～0x14，频率uint16、5～1000 Hz步进5，且不超过目录maxHz；V1+V2总额不超过8000标量每秒。一个变量只保留一个版本订阅，后者替换前者；失败批次不改变任何订阅。最多32个实际(version,rate)组，按固定目录半区每包最多16项，以组槽/半区作为LATEST key，避免不同频率互相合并。真实sampleTime、sequence、valid来自业务发布缓存，encodeTime只表示HostComm编码时间；断连不阻塞Publish。详细字节布局见 [SOURCE V2协议](../../../Docs/HostLink.md#只读-source-v2)。

## IMU 静态校准命令

CONFIG `0x09` 将手动静止校准交给已有 HostCmd，`0x0A` 在 HostComm 读取进度。正文 `01 03` 选择双 IMU，约 5 s 采集，仅估计陀螺零偏；EEPROM 读回验证后返回成功，异常返回错误及部分保存掩码。总执行期限 20 s，不含排队；HostCmd 期间不能处理其他耗时任务，HostComm 状态查询/遥测继续。新值下次启动生效，断连不取消已执行工作。默认固定路由占 22/24 项，仍执行冲突/冻结检查，无新增线程、队列或栈预算。协议、去重期限和错误编码见 [线协议](../../../Docs/HostLink.md#手动静态-imu-校准)。

完整外层字段、编码样例及全部服务/消息/流 ID 见 [COBS 与协议总表](../../../Docs/HostLink.md#cobs-帧与校验)；主机存储交互见 [EEPROM 设计](../../../Docs/Storage.md)。
