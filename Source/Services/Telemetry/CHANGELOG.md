# Changelog

## 2026-10-20

- 文档补充共用 handle 的图传出口及注册先后顺序：HostLink 准备时已冻结目录，bind_channels 只绑定 VideoLink 路由。现有 Telemetry 缓存、类型及 HostLink 线协议不变。

## Unreleased

- 2026-09-29：链接统一消息表和 SOURCE V1/V2，明确遥测与参数目录及身份分离；实现未改。

- 新增独立于通信初始化的只读变量目录、C 注册/发布接口及 C++ `SRNOS::Telemetry::Value<T>`，支持四种标量类型、最多32项和静态生命周期。
- SDK 保存最新值、发布序号、真实采样时间及有效性，Publish只做单项短临界区更新，断连不阻塞生产者。
- SOURCE V1改从缓存读取并保持线格式；SOURCE V2通过独立消息号提供1000 Hz单源/8000标量每秒总限额、版本互斥订阅及固定频率组LATEST分包。
- 新增纯主机C/C++目录测试和HostComm协议回归；不包含板测或上位机实现。
