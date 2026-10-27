# 只读遥测变量

`Telemetry.h/.hpp` 提供与 SirinLink 初始化顺序分开的启动目录及最新值缓存。父项目静态声明只读观测变量，在业务任务得到新样本后显式发布；SDK 不读取任意 `volatile void *`，也不调用业务 getter。这里的变量不进入 Parameters 可写目录。

```cpp
#include "SirinEmbedOS/Services/Telemetry.hpp"
static SRNOS::Telemetry::Value<float> temperature{"motor.temperature", "C", 1000};
// 在业务 step 取得新样本后调用；sampled_us 来自实际采样时刻。
SRNOS_TelemetryResult publish_temperature(float value, uint64_t sampled_us, bool valid)
{
    if (temperature.Status() != SRNOS_TELEMETRY_OK) return temperature.Status();
    return temperature.Publish(value, sampled_us, valid);
}
```

只支持 `float`、`int32_t`、`uint32_t`、`bool`，模板拒绝其他类型。`Status()` 查询登记结果；`Publish(value, sample_time_us, valid=true)` 返回 SDK 结果。对象应为静态生命周期，不能复制；不要在业务 step 中构造。名字、单位和描述由 SDK 有界复制，目录最多 32 项，名称最长 63 字节且唯一、单位最长 15 字节。`maxHz` 为 5～1000 Hz、步进 5，是主机允许订阅的上限，不是自动采样定时器。

C 使用 `SRNOS_TelemetryDescriptor`、`SRNOS_TelemetryRegister(&descriptor, &handle)` 和 `SRNOS_TelemetryPublish(handle, value_union, sampled_us, valid)`；调用者必须按登记类型填写 union 并检查登记返回值。登记只允许调度启动前。SirinLink setup 完成后冻结目录；SirinLink 关闭时 Kernel 也冻结目录。冻结后不能增加、删除或改描述；失败登记不占目录槽。

同一 handle 可注册 [VideoLink](../VideoLink/README.md) 图传路由，共享最新值、类型和名称。开启 SirinLink 时，在静态初始化或 ApplicationConfigure 中登记字段；`bind_channels` 阶段只绑定图传周期，此时 SirinLink 已冻结 Telemetry 目录。图传周期独立于 SirinLink 的 `max_hz` 订阅上限。

Publish 仅允许冻结后的任务上下文，ISR 与非运行调度状态返回 CONTEXT。它只在短临界区内复制一个标量、64 位采样时间、有效位并递增 32 位序号；不分配、不扫描目录、不等待、不写日志/设备、不唤醒 HostComm。首次发布前序号为 0 且无效；无效发布保留调用者的值和时间，不能被解释为新有效测量。序号允许无符号回绕，断连期间仍可独立发布。多次发布覆盖旧值，不提供无损历史缓存。

HostComm 在自己的截止点逐项取副本并编码，编码不持目录临界区；不同变量不是跨任务原子快照。V1 SOURCE 保留低频线格式，V2 提供真实采样时间、序号、有效位和 uint16 频率。每个变量最多一个 V1 或 V2 订阅，后订阅替换前订阅；主机订阅不改变发布频率。协议详见 [SirinLink 协议](../../../Docs/SirinLink.md#只读-source-v2)。

SirinLink 专项覆盖 V1 回归、V2 限额/替换、固定槽分包、调度回绕、背压及断连。测试未证明 1000 Hz/8000 标量每秒在任意 USB 负载下均可达到；板端开销和吞吐仍待实测。

目录与业务参数目录彼此独立；所有消息号、COBS 帧及 V1/V2 对照见 [协议总表](../../../Docs/SirinLink.md#服务与消息总表)。
