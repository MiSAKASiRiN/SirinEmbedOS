# SRNOS 命名、公共隔离与事件等待板端复测

2026-09-28，用户明确授权烧录并验证。当前源码基于 [主机/构建验证](../SRNOSBoundary-20260928/README.md)，本轮未修改生产运行逻辑、CubeMX 输出、采样频率、线程优先级或内存布局。只修正独立板测工程的编译装配：原生内核/ADC 注入适配在单独 OBJECT 目标，应用 `app.cpp` 仍只获得公共头路径。

## 固件与调试现场

- 连接前 CLion 调试会话为空，无其他 GDB/J-Link server。明确选择 J-Link OB `10114514`，V8.96，SWD 4000 kHz，STM32H723VG，VTref 3.30 V；运行计数时钟 550 MHz。
- 先保存原 1 MiB Flash。测试 ON/OFF 及最终正常固件均用匹配 ELF 烧录，七个加载段全部 `compare-sections` 一致。
- 最终保留根 Minimal **Release**，默认双 IMU/INS 与 60°C 加热策略、SystemMonitor/循环统计启用，普通 UART 未绑定。没有 ADC 注入包装，没有测试 ControlLoad 任务；未写 EEPROM、未发送外部 CAN 控制帧。
- 最终 ELF SHA256：`1f77906558c046427a2a22c6506a37031ad5a0f519220191943e18f996f5b36d`。原件 `build/Release/SirinEmbedOS.elf`，本轮副本 `build/srnos-board-20260928/final-release.elf`。
- 正常固件退出调试前活动 Fault 标志为 0，heap 当前余量 66024 B、最低 61800 B。随后 GDB detach、server 退出；一分钟传感器及 20 秒监测采集均在释放探针后完成。
- 用户回复“无”：没有仪表读数/外部 CAN 负载资料。不能据近零 ADC 值宣称电压电流精度通过。

ON 测试 ELF SHA256：`33a9d3def420c8c46db56a741a23a4e2b1b0c382cbd59a77370661f67fb63b61`；OFF：`00915c159a5372b7966d8b847dc3c551b0742ab37b2fb97b9273ad2565482e57`。两者为 Release 优化加 `-g3`，关闭加热、开启 Monitor 和四颗传感器/INS，增加 1 ms 周期、约 20 µs 工作的应用任务。

## 统计与生命周期

每次启动先执行生命周期测试，稳定 5 秒后连续采集 30 个一秒窗口。窗口内不暂停 CPU、不订阅高流量遥测。四次正式样本均确认 `stage=3`、`samples=30`。首次预采集在第 24 个窗口被提前暂停，明确剔除 CPU/连续性比较，原始文件保留为 `on-interrupted-24.*`。

| 完整窗口 | 系统平均 CPU | Monitor 平均 CPU | 监测临界区最大值 | 完整 hook 最大值 |
|---|---:|---:|---:|---:|
| OFF 第一组 | 21.33155% | 0.04054% | 2 µs | 0.162 µs（关闭快速路径） |
| ON 第一组 | 22.32310% | 0.04918% | 2 µs | 0.680 µs |
| OFF 第二组 | 21.33409% | 0.04055% | 2 µs | 0.184 µs（关闭快速路径） |
| ON 第二组 | 22.32397% | 0.04912% | 2 µs | 0.680 µs |

统计开关差值分别 **0.99155 / 0.98988 个百分点**。本负载满足 ≤1 个百分点，但余量很小；它包含开关导致的实际运行与内存分配差异，不等于单独 hook 的理论成本。后续仍应优化并在父应用、CAN/CDC 组合负载下重测，不能宣称已有充分余量。

hook 基准覆盖完整 Begin/End，逐次屏蔽中断、128 次间阻塞一 tick，只减去两次 DWT 读取成本。正式 ON 平均 345 cycles / 0.627 µs；预采集启动基准曾观察到 419 cycles / **0.762 µs**，仍小于 1 µs。该值不包含抢占，不能作为任意缓存状态或负载下的绝对最坏值。最长临界区 2 µs 只覆盖监测/循环统计测量点，不是全系统最长关中断。

每个正式窗口均为 400 次创建/Join/Delete、2846 项检查、零失败，合计 1600 次生命周期、11384 项检查。覆盖 init 失败、停止唤醒、事件通知、协作删除超时、持锁/等待时对象删除拒绝、TLS/errno 与清理、旧句柄、池耗尽及资源回收。父对象池前后用量均为 0，峰值 30208/32768 B；一次预期的资源耗尽与创建失败被正确计数。

每个 30 秒窗口 INS、BMI088、LSM6DS3 都新增 30000 次周期/样本，未增加 missing、missed_deadlines、设备 errors/overruns/recoveries。环境传感器继续 READY。四路 CAN 的 RX 出队和 TX 接纳计数均为 0；CAN4 的 `tx_complete_events=2` 是启动时已有的发送空位通知，窗口内没有增加，不能把该字段当成实际发送帧数。这只能证明空载共存，不能证明高负载 CAN。

数据：[比较](comparison.json)、[ON](on.json)、[OFF](off.json)、[ON 重复](on-repeat.json)、[OFF 重复](off-repeat.json)。

## ADC 与通信

ADC 正常转换约 211–212 µs，五路持续有效，低电流质量位正常。最终监测样本：输入 0 V、电流 0.002625 A（低精度区）、VBAT 3.3062 V、VDDA 3.3151 V、VREFINT 1.2362 V、芯片内部温度 87.38°C。MCU 内部温度不是 IMU 温度；这些是固件换算值，没有仪表精度依据。最终 errors/timeouts/recoveries 均为 0。

独立测试固件依次注入四种状态，真实 ADC 转换/停止仍执行：

| 注入 | 实际结果 |
|---|---|
| 完成报告错误 | errors +1、valid_fields=0，保留上次数据/时间；下一秒恢复 |
| 丢失完成通知 | timeouts +1，失效后重新校准并恢复；旧采集时间没有伪装为新数据 |
| 完成后线程晚处理 20 ms | age 约 34.8 ms，仍有效，errors/timeouts 不增加 |
| 无法确认停止 | ADC 进入 QUARANTINED，序号和采集时间保持、有效位清除；系统快照/任务统计继续更新，未 Panic |

第四项仅模拟停止确认失败，硬件实际停止；不等于测试物理 BDMA 卡死。结果见 [ADC 注入数据](adc-fault-results.json)。正常固件不含注入开关。

CDC1 同时订阅三条监测与五条传感器流，正常读 3 秒 → 停读 12 秒 → 排空 5 秒 → DTR 低 5 秒 → 重连读 5 秒。实际 LATEST 合并 24 项、队列峰值 11、最长等待 8370 ms；重连后三条监测流恢复，主机解码错误 0，ADC 序号在约 30 秒内增加 30。证据见 [CDC 元数据](cdc-metadata.json)、[板端 HostLink 状态](hostlink-after-cdc.json)。这是 DTR 会话和读背压，不是物理 USB 拔插；未测长业务命令/满活动表/持续 RX 的板端组合压力。

## 正常固件独立采集与温稳边界

探针释放后使用 `imu_capture.py --streams bmi lsm ins mag pressure --warmup 5 --duration 60`，五条流正式窗口均无缺序、重复、乱序、时间回退或无效样本，COBS/CRC 等协议错误为 0。CDC0 同时可读到 `SRNOS_Log` 启动日志；不等于日志队列压力验收。

| 流 | 正式样本 | 设备时间计算速率 |
|---|---:|---:|
| BMI088 | 60001 | 1000.0007 Hz |
| LSM6DS3 | 60001 | 999.9987 Hz |
| INS | 60001 | 1000.0000 Hz |
| LIS2MDL | 5906 | 98.4331 Hz |
| SPA06 | 979 | 16.3272 Hz |

退出订阅时 BMI/LSM/INS 尾部各 4/2/1 条未发送记录按取消合同丢弃；不属于正式窗口缺序。采集前后 INS missing_cycles 保持启动期的 16，missed_deadlines=0、epoch=1，健康 mask=3，未出现过温保护或硬件加热故障。

**温稳尚未作为通过项。** 60 秒内 LSM 反馈 59.8945–60.7227°C，均值 60.2759°C；BMI 为 57.0–57.625°C。最长连续目标 ±0.25°C 区间约 13.695 秒。按现有连续 5 秒判据，INS 正常 8.696 秒，其余约 51.305 秒带 THERMAL_PENDING 降级；姿态始终有效、双路参与。60 秒预热超时标记保留。后续只读查询在运行约 186 秒仍为 60.3555°C、DEGRADED、flags=0x331。不能把温度接近目标、数据连续当作恒温已验收，也不据此直接放宽阈值；需更长热平衡记录，复核 PI 收敛、温度噪声及温稳进入/保持策略。

见 [传感器采集元数据](final-sensors-metadata.json)、[温度/状态分析](thermal-analysis.json)、[后续 INS 状态](final-ins-status.json)。随后三条 Monitor 流 20 秒采集各 21 帧、解码错误 0，ADC 无错误/超时、任务槽无溢出、未跳过监测周期，见 [监测元数据](final-monitor-metadata.json)。

## 构建、资源与剩余验收

正常 Release 重建、两个独立板测构建通过；新增应用编译命令不含私有路径，[命令证据](application-compile-command.json)。453 项结构合同再次通过；完整主机矩阵仍引用前一阶段结果，没有将它记成本轮重新执行。默认周期扫栈仍关闭，当前未取得线程栈的压力峰值证据。

生产 Release D1 307008/327680 B，链接余量 20672 B；D2 9696 B、D3 544 B，ADC 仍在 `0x38000200`、32 B 对齐。DTCM 100% 为 heap/MSP 固定预留；不等于 heap 已耗尽。测试固件 D1 310240 B，不与生产值混用。

还需参数/INS 持久化迁到 FT24C128A、真实 CAN/SPI 并发、UART/RS485 时序、物理故障恢复、ADC 仪表对照、长期 FPU/libc/同步压力、实际 USB 拔插和受控栈水位采集。统计成本接近上限、温稳反复退出是本轮具体优化线索。IWDG/WWDG 监督与掉电一致性属于后续功能开发；FatFs/LwIP/MSC/DFU 按实际父项目需求接入。优先级见 [开发清单](../../Development.md)。

完整原始串口、CSV、Flash 备份和 GDB 日志保存在本地 `build/srnos-board-20260928/`，其 SHA256/大小见 [采集清单](capture-manifest.json)。本目录保留紧凑结构化证据；原始大文件属于 build 产物，归档前需单独保存。未提交、推送或发布固件。
