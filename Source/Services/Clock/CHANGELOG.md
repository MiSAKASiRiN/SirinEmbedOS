# Changelog

## Unreleased

- 2026-10-21：新增任务上下文 Clock 公共 API，统一读取 DWT uptime 与有效性明确的 Unix UTC 微秒；未授时只提供 uptime，不恢复历史绝对时间。
- NTP 更新仅改变绝对时间映射；记录 generation、校正量、RTT、来源层级与 holdover，保留原有 RTOS tick/控制超时。支持用同一快照转换过去的采样时间。
- Clock 与真实 lwIP NTP 主机回归通过；父项目固件完成构建检查。未烧录、未连接 Jetson，实机精度待验。
