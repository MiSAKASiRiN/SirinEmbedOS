# Changelog

## Unreleased — srneos 包复用（2026-11-04）

- 主机脚本支持包内相对导入并保留直接执行方式；统一 CLI wheel 将同一份源码安装为 `srneos._legacy`，没有新增第二套 COBS/CRC/SirinRMT 编码器。
- 未修改线协议、设备固件或原独立脚本的发送策略；统一 CLI 的会话和收尾行为见 CmdLineTool。验证范围为主机导入/CLI/协议与打包检查，本轮未连接真实设备。

## Unreleased — 直接平面速度

- 增加 planar_velocity() 与 --velocity=vx,vy,wz，有限 IEEE754 float32 编码；--velocity 与 --channels 互斥，通用 CLI 上限允许 100 Hz。C/Python 黄金向量、CLI 和 UDP 直接目标回归通过；Jetson 100 Hz 与架空两模式联调通过。

## Unreleased — UDP raw codec

- 2026-10-21：抽取 pack_raw/unpack_raw 供 UDP 共用 V2 长度/CRC 校验；pack/unpack 的 COBS 行为保持，SirinRMT wire 直接选择传输封装。C/Python 黄金向量、父项目 UDP loopback 和 IMU/INS CLI 回归通过；父项目已完成 Jetson/STM32 双向 100 Hz、六方向与停止联调。

## 2026-10-21 SirinLink 与 DISABLE 工具

- 工具目录、导入和描述统一为 SirinLink；外层 V2 与 SirinRMT v1 不变。新增 disable(session, sequence) 与互斥 --disable/--enable；--disable 只发送一帧，不补 CHANNELS/INVALIDATE。
- 增加 --session、--sequence、--local-port 供同一发送端保持所有者身份；活动会话不能被随机会话接管。C/Python 黄金向量及 4 项父项目 UDP loopback 通过，未连接硬件。

## Unreleased — 输入与消息

- 2026-10-21：新增 `enable(session, sequence)` 及显式 `--enable`，输入开始 100 ms 后只发一次请求，默认关闭，退出仍 INVALIDATE。C/Python 黄金向量和父项目三项 UDP loopback 回归验证；未连接实际接收机或执行器。

- 2026-10-21：增加 sirin_rmt.py，C/Python 黄金向量验证，支持限时 UDP 或 CDC/UART 输入模拟及离线 hex。

## Unreleased

- 2026-09-29：文档明确现有 CLI 支持 IMU/INS/监测接收保存，不包含参数编辑或静态校准触发界面；与当前固件协议入口互链。

- 2026-09-28：修复主机把 SAMPLE message=0x02 误写为 0x03 导致三流静默过滤的问题，同步回归与协议说明；实板接收、背压及 DTR 重连通过。

- 2026-09-28：查看/保存工具支持循环统计 0x0003 和旧双流固件，展示统计口径并保存实际支持流信息。验证范围为主机与构建；本次未烧录，时序与开销仍待板测。

- 2026-09-28：SystemMonitor替代Diagnostics线程，1 Hz ADC/任务统计与HostLink发布；增加ADC板级BDMA接入、逐槽统计和主机工具，默认取消扫栈。迁移system_monitor_enabled及HostLinkPrepareEx合同；Indicators/生成文件不变。验证范围为主机与构建，未烧录/板测。

## 2026-09-28 板端验证

SystemMonitor SAMPLE 帧使用 kind=2、service=4、message=2；流 ID 位于 payload，循环流 0x0003 不是 message=3。板端已验证三条流接收和 DTR 重连。
