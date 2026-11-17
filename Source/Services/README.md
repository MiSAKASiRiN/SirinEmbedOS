# 系统服务

父项目在 `SRNOS_ApplicationConfigure()` 中选择服务和声明资源。SDK 在调度器启动前校验并冻结配置，然后装配 worker；应用不要手动重复调用 Prepare/Start。服务创建成功、外设就绪、主机已收到数据是不同状态，应分别查询。

## 按需求选择接口

| 需求 | 使用方式与示例 | 主要边界 |
|---|---|---|
| 文本日志 | [Logging](Logging/README.md) | 有界非阻塞入队，成功不是实际交付 |
| USB 命令和遥测 | [SirinLink](SirinLink/README.md)、[USB](USB/README.md) | CDC0 文本、CDC1 二进制，声明在启动期冻结 |
| 可保存业务参数 | [Parameters](Parameters/README.md)、[协议](Parameters/SirinLink/README.md) | WRITE 只改 RAM，SAVE 显式持久化 |
| 高频只读数值 | [Telemetry](Telemetry/README.md) | 最新值缓存，不提供无损历史队列 |
| VT03/VT13 遥控与双向图传 | [读取摇杆与键鼠](RemoteControl/README.md#vt03vt13-接入) · [VideoLink](VideoLink/README.md) | 921600/8N1；基础遥控只需 RX，回传遥测时启用 TX 和 VideoLink |
| VT02/VT12 键鼠与自定义数据 | [VT02](RemoteControl/VT02/README.md) | 115200/8N1；分别处理 0304 键鼠、0302 原始消息，支持显式 A5 发送 |
| EEPROM 身份和记录 | [Storage](Storage/README.md) | 单份记录，CRC 不保证保留写入前版本 |
| 姿态与 IMU 校准 | [INS](INS/README.md) | 使用有效位/年龄/epoch，不能只看非零值 |
| UDP 与 Ping | [Network](Network/README.md) | 需同时启用 Ethernet；无送达保证 |
| 启动与绝对时间 | [Clock](Clock/README.md) | 可选 NTP；绝对时间校正不改变控制时基 |
| 遥控输入 | [Input/SirinRMT](Input/README.md)、[RemoteControl](RemoteControl/README.md) | 各来源独立快照/事件，控制映射与仲裁归应用 |
| 通用业务消息 | [Message](Message/README.md) | 启动期订阅服务/类型，队列或短回调分发，payload 复制或限时借用 |
| 裁判协议与 UI | [Referee](Referee/README.md) | SDK 编解码，应用决定 UI 内容与刷新 |
| 系统健康与任务观测 | [SystemMonitor](SystemMonitor/README.md) | 副本、有效位和独立采样窗口；不周期扫栈 |
| 提示音与心跳 | [Indicators](Indicators/README.md) | 固定服务装配，应用通过设备 API 提交 |

## 最小使用示例

一个最终固件只定义一份配置入口；将需要的字段合并到已有入口，不直接重置整个结构：

```c
#include <SirinEmbedOS/Kernel/System.h>
#include <SirinEmbedOS/Services/Logging/Log.h>

void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config)
{
    config->usb_enabled = true;
    config->logging_enabled = true;
    config->sirin_link_enabled = true;
    config->system_monitor_enabled = true;
}

void SRNOS_ApplicationMain(void)
{
    SRNOS_LogResult result = SRNOS_LOGI("application started");
    (void)result; /* 需要统计失败时自行计数，不递归记录日志。 */
}
```

配置回调只声明，不阻塞 I/O、不创建线程；名字、参数和回调对象应满足各接口的生命周期要求。`ApplicationMain` 在参数加载尝试和 USB 启动后放行，可以返回；业务任务使用 [Kernel 托管接口](../Kernel/README.md)。

## 共同约束

- SDK 配置不进入可远程写入的业务参数表。CAN 模式、UART 角色、服务开关和线程容量由父项目启动声明。
- 普通 UART 默认未绑定。日志 UART、遥控、裁判等需要显式选择互不冲突的 Serial 用途；`bind_channels` 声明额外通道所有权或 VideoLink 遥测路由，不重新读取已归 SDK 服务所有的端口。
- ISR 只使用明确标为 FromISR 的 API。持久化、长命令和校准在管理任务执行，不能阻塞实时控制回调。
- 原始传感器/INS 流由已有 HostComm 编码；订阅与 USB 背压不改变传感器采样周期。遥测启动前检查功能选择，`ins.enabled=false` 不是线程和静态内存裁剪开关。
- 系统监控、日志、网络和 SirinLink 的固定槽容量不会随注册数量自动扩容。以最终 map、Kernel 资源快照与板端测量分别核对静态、动态和执行预算。

协议布局见 [SirinLink](../../Docs/SirinLink.md)，跨模块说明见 [文档导航](../../Docs/README.md)。
