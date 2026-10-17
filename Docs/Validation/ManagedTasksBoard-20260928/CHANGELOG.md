# 验证记录

本文保存对应归档的原始报告。日期、版本、通过范围和结束状态仅对当时现场有效，不表示当前板上状态。当前阅读方法见本目录说明文件。

---

<!-- 原报告：README.md；原文 SHA256：ba765cdc078959ec72c6783b2fbe8585b8c270e2abdcd8d07d8122ac9dc62492 -->

# 托管任务与 SystemMonitor 板端验证（2026-09-28）

本轮由用户明确授权烧录和实测。板卡 STM32H723VG，CPU 550 MHz；J-Link OB 序号 10114514、目标电压 3.30 V、SWD 4000 kHz。连接前确认没有 CLion 调试会话/其他 GDB 占用。使用 J-Link V8.96 和 ARM GDB，保存原 1 MiB Flash 后烧录。未执行 CubeMX Generate、EEPROM 写入、CAN 控制发送或提交代码。

## 发现和修复

- 首次 ADC 转换实际完成，但 HAL 在 BDMA normal 完成后将 DMA 状态置 READY、仍保留 CCR.EN。`HAL_ADC_Stop_DMA` 因非 BUSY 跳过 Abort，原停止确认误报 QUARANTINED。板级适配现先由 HAL 停止 ADC，再显式关闭专用 BDMA 通道并读回状态；继续要求 ADC/BDMA 均停止，未放松保护条件。主机假 HAL 已复现 READY/EN 行为。
- 主机工具把 SAMPLE 的 message 误写成 0x03，导致握手成功却静默过滤所有监测报告；实际协议为 kind=2/service=4/message=2。修复工具、回归数据和协议说明，固件线协议不变。
- 初版完整 hook 无抢占均值 1.55 µs、最大 1.59 µs，未达目标。Release 对循环统计局部使用 -O2，直接读取已初始化 CYCCNT，复用每 tick 更新的 64 位锚点，并省去事件任务的周期计算。最终完整调用基准如下；没有以遥测中的不完整成本估计代替验收。

## 同负载对照

独立父项目 `Tests/Board/ManagedTasks`，Release 优化并保留调试信息。两组均关闭 IMU 加热、启用 SystemMonitor、运行所有板载传感器/INS、另运行 1 ms 周期且约 20 µs 工作量的应用任务。5 秒稳定后采集 30 个一秒窗口，窗口内没有暂停 CPU 或打开高流量遥测。`on-fast.json` / `off-fast.json` 均 stage=3、samples=30；原始内存副本与 GDB 日志同目录。CPU 用实际 DWT 间隔与 Idle 累计周期差计算，含 RTOS 的中断归属。

| 指标 | 结果 | 本次目标 |
|---|---:|---:|
| 全系统忙比例，循环统计关闭 | 36.0654% | 基线 |
| 全系统忙比例，循环统计开启 | 36.6190% | — |
| 开启循环统计的 CPU 增量 | **0.5535 个百分点** | ≤1 个百分点 |
| SystemMonitor 开启循环统计时平均 CPU | **0.05176%** | ≤0.1% |
| 采样/快照/统计最大临界区 | **2 µs**（向上取整） | ≤10 µs |
| 完整 Begin+End 无抢占平均 / 最大 | **0.6509 / 0.7000 µs** | ≤1 µs |

hook 基准 128 次，每次只在一对 hook 调用期间屏蔽中断，批次之间阻塞 1 tick；减去两次 CYCCNT 读取的 1 cycle。原始值 min/mean/max = 310/358/385 cycles。它不是冷缓存或任意负载最坏时间证明，Debug 时序也未据此宣称达标。

两组各完成 400 次创建/Join/Delete、2846 项检查，失败 0。覆盖抢占创建、TLS/errno 初始化与隔离、init 失败、等待中停止、事件通知、删除超时、互斥/队列进行中删除拒绝、句柄失效、池耗尽和清理后回收。父项目池使用量均回到 0，峰值 30208/32768 B；预期的资源不足计数各 1，不触发 Panic，RTOS 空闲堆未因重复生命周期减少。

两组 INS 均新增 30000 轮/发布，无新增 missed_deadlines、missing_cycles 或 recoveries；双 IMU 各新增 30000 样本，无错误/恢复。LIS 新增 2968/2959、SPA 各 489 样本，同样无错误。四路 CAN RX/TX 均无实际帧流量，本次不构成外部 CAN 压力验证。

## ADC、CDC 和故障

用户说明当前输入电压/电流“应该都是 0”，未提供仪表读数。实板外部两通道接近零，电流低精度标志有效；内部通道持续有效，VBAT/VDDA 约 3.3 V，MCU 温度换算约 84–88°C（仅片内读数，未做外部温度对照）。实读出厂 12 位校准值 VREF=1534、TS30=772、TS130=1018。`vrefint_v` 由同组出厂校准计算，不能当成独立电压精度基准。

`cdc-final` 同时订阅三条监测和五条传感器流，停读 12 秒后恢复，再拉低 DTR 5 秒并重连。报告解码零错误，重连收到 tasks/ADC/cycles；板端遥测 QoS 记录 coalesced=24、队列最高 11、最长等待 8366 ms，证明实际出现背压而采用最新值合并。首尾 ADC 序号增加 30、完成时刻差 30.019017 秒，错误仍为零。DTR 测试不等于物理拔插。

仅测试固件通过链接包装注入，共消耗 5 次命令（晚处理重复 2 次以完整抓到遥测）：

| 注入 | 观察 |
|---|---|
| 完成报告错误 | 当轮撤销有效位，保留上次值/序号/时间；下一轮恢复，errors=1/recoveries=1 |
| 丢失完成结果 | timeouts 增至 1，恢复后 errors=2/recoveries=2；持续 READY |
| 完成后延迟处理 20 ms | 捕获数据年龄 34819 µs、转换实际约 212 µs；仍 READY，无新增错误/超时 |
| 停止确认失败 | QUARANTINED，valid_fields=0；ADC 序号固定 250、保留原值，年龄递增；系统快照从 283 继续到 288、22 个任务继续统计 |

真实 ADC 启动/完成/停止仍执行；最后一项是“无法确认停止”的软件模拟，硬件实际安全停止，不能据此宣称物理 BDMA 卡死已测试。复位和最终生产镜像已移除注入状态，注入包装不参与生产构建。

## 构建、内存和交付

22 组主机套件通过；九个父项目固件、容量覆盖/溢出拒绝、第三方哈希、结构和 IRQ/内存检查通过；Minimal Debug/Release 均重新构建。证据为本目录 host/sdk/structure/elf 日志。本轮修改通过定向 diff 检查；全仓 diff-check 仅报告此前生成文件 i2c.c/main.c 的 CRLF 行，本轮未改写。

最终正常 Minimal Release 来源 `build/ManagedTasks-Release/SirinEmbedOS.elf`，独立副本 `build/managed-board-20260928/final-release.elf`。烧录后 `.isr_vector`、`.text`、`.rodata` 逐段比较 matched。Flash 使用 187800 B；D1 305504/327680 B（含预留区域，93.23%）；D2 9696 B；D3 544 B。ADC 缓冲地址 **0x38000200**，32 字节对齐，保持 D3 `.bdma_buf`。原 Flash 备份及测试 ELF 保留于上述 build 目录；摘要哈希见 `summary.json`。

最终板卡运行正常 Release 固件，测试用 ControlLoad、故障包装及关闭加热配置均不在生产镜像中；回到 Minimal 默认配置（含默认加热设置）。GDB 无断点，已 detach/quit；本轮 J-Link Server 已退出释放探针。退出调试器后的独立 CDC 抓取见 `final-release-telemetry`，三条流各 31 帧、解码零错误、ADC 全部有效且错误计数为零，确认目标继续运行。

尚未覆盖：仪表电压/电流/温度精度、多点标定、外部 CAN 持续/突发负载、物理 USB 拔插、真实 ADC/BDMA 卡死、长时间压力与专用 ISR 同步压力、Debug hook 开销。以上 Release 短测结果限定于本次板卡和负载。
