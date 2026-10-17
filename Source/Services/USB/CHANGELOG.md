# Changelog

## 2026-10-17 文档整理

- README 按当前使用方式、示例和约束组织；不在使用文档追加版本战报。

## Unreleased

- 2026-09-28：自维护设备句柄、CDC 发送、接口表及描述符表改为 `SRNOS_USB*`，单文件描述符函数/数据、PCD 指针及三个辅助函数改为私有静态对象；保留 HAL/USBD_LL ABI。CDC 分发、发送所有权、启动失败/重试主机测试通过；USB 枚举和板端行为仍待验证。

- 2026-09-28：父应用只调用公开 `SRNOS_USB*` 能力；CDC transport、描述符、PCD 和 class context 是 SDK 私有实现。自维护跨文件符号采用 SRNOS 命名，HAL/USBD_LL 规定回调保留原 ABI；USB PID/接口字符串、双 CDC 端点和线数据保持。Platform 不向父项目导出 middleware 或 HAL include。 本轮主机/构建范围见 `Docs/Validation/SRNOSBoundary-20260928/README.md`；未重新烧录。

- 2026-09-28：CDC 自维护调用统一 SRNOS 命名，内部通道标识改为 HOST_LINK；USB/Middleware 规定入口及描述字符串不变。日志 sink 和 HostLink 通知接入私有接口；本轮没有枚举或拥塞板测。

- 2026-09-28：完成 Support 职责整理；接口/时序/调度保持，固定硬件配置改为板级私有。21 组主机测试与固件集成构建通过；未板测。
