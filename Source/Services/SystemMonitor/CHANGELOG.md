# Changelog

## Unreleased

- 2026-09-28：首次 24 V/0.091 A 仪表单点对照，13 次 ADC 电压均值 24.641 V（+2.67%）、电流 0.08986 A，采样错误 0；未同步仪表，不构成标定。本轮保持换算系数，CAN 稳态压力下 Monitor 快照 CPU 最大 0.0571%、最长临界区 2 µs；见 [记录](../../../Docs/Validation/CAN13-ADC-20260928/README.md)。

- 2026-09-28：SRNOS 当前固件复测 Monitor 约 0.0492%、监测临界区 2 µs；四项 ADC 软件注入及三流背压/重连通过，最终正常固件 ADC 错误为 0。近零输入未做仪表精度对照；见 Docs/Validation/SRNOSBoard-20260928。

- 2026-09-28：完成零输入/内部通道读取、完成错误/丢失/晚处理/停止确认失败注入与后台持续统计板测；BDMA 停止误隔离修复见 Board/Support。模拟精度及物理故障仍待验收。

- 2026-09-28：发布托管循环快照、有效性和最大采样临界区，维持 Low/4 KiB 与四槽批次。 主机/构建证据见 `Docs/Validation/ManagedTasks-20260928/README.md`；本次未烧录，时序与开销仍待板测。

- 2026-09-28：新增 Low/4 KiB SystemMonitor，替代 Diagnostics，ADC 五通道 1 Hz 与逐槽线程统计共用一个线程；Indicators 不变。
- ADC 使用 BDMA、任务完成通知、10 ms 超时和停止确认（含无效绑定隔离）；新增校准换算、质量/有效性、旧值与过期合同。
- 新增任务生命周期 generation、实际窗口/CPU ppm、短临界区观测；关闭周期扫栈，不增加循环埋点。
- 新增两条最新值遥测流和 JSONL 主机采集；22组主机套件、Debug/Release及九个父项目构建通过；ADC D3/对齐与结构检查通过，详见Docs/Validation/SystemMonitor-20260928，尚无板端验收。
