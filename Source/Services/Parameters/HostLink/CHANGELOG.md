# Changelog

## 2026-10-17 文档整理

- README 维护当前使用方式、示例、资源和限制；变更经过与验证结果集中记录于 CHANGELOG。

## Unreleased

- 2026-09-29：补齐 CONFIG 01–05 请求/响应字段、值类型、分页、WRITE 原子性、SAVE/LOAD 状态及交互顺序；保留 06–08，链接独立 IMU 校准 09/0A。无消息编码变更。


- 2026-09-28：保留 CONFIG 01–05 编码，增加 06 持久化状态、07 只读 PCB 身份及 08 稳定 ID 分页目录。SAVE/LOAD 继续由 HostCmd 执行；新增 BUSY/CONFLICT/禁用映射。协议专项通过，本轮未烧录。

- 2026-09-28：参数协议统一 SRNOS_ 命名，注册采用 HostLink 不透明启动上下文，公共头不依赖 RTOS。保留 CONFIG 线格式与存储权限；参数 HostLink 主机测试通过，本轮未烧录。
