# CAN1 / CAN3 Classic 与 CAN FD 实板通信、负载

2026-10-05 夜至 10-06，用户确认 PCB 在线、CAN1 与 CAN3 短接后执行。Classic 8 B、FD 8 Mbit/s 64 B 均连续 60 秒达到**每控制器同时 3000 RX＋3000 TX 帧/s**；FD 5 Mbit/s 64 B 的同档 10 秒通过。4 Mbit/s 及以下的 64 B 同档未达到请求速率，出现可计数的发送背压/TTL；不是 CPU 跑满或接收数据损坏。

## 目标负载结果

仲裁段均为 1 Mbit/s。每个窗口请求 CAN1→3、CAN3→1 各 3000 帧/s，在同一物理总线上合计 6000 帧/s；每帧一次发送、一次接收，不能再将两端统计之和当作线上帧数。

| 配置 | 时长 | 活动窗口实际帧/s（1→3 / 3→1） | 整板 CPU | 拒绝 / TTL 过期总帧数 | 结论 |
|---|---:|---|---:|---|---|
| Classic 1M / 8 B | 60 s | 3000.0 / 3000.0 | 32.00% | 0 / 0 | 通过 |
| FD 1M / 无 BRS / 64 B | 10 s | 1664.1 / 0.0 | 29.06% | 36876 / 6336 | 未达标，带宽/仲裁背压 |
| FD 2M / BRS / 64 B | 10 s | 2999.9 / 169.8 | 30.98% | 23573 / 4637 | 未达标，带宽/仲裁背压 |
| FD 4M / BRS / 64 B | 10 s | 3000.0 / 2786.5 | 38.63% | 2045 / 0 | 未达标，带宽/仲裁背压 |
| FD 5M / BRS / 64 B | 10 s | 3000.0 / 3000.0 | 35.87% | 0 / 0 | 通过 |
| FD 8M / BRS / 64 B | 60 s | 3000.0 / 3000.0 | 35.13% | 0 / 0 | 通过 |

Classic 和 FD8M 的两个 60 秒窗口各交付 360000 帧，逐帧序号、全部 payload 与 flags 正确，无背压/过期/缺序。Classic 各 4k 的 10 秒也完整交付、CPU 36.96%；FD8M 64 B 各 4k 的 5 秒完整交付、CPU 40.73%。Classic 各 5k、FD5M 各 4k 超载。所有 FD 速率的 **8 B 各 3k / 10 秒**均通过，包括无 BRS 1M。

在本次测试过的速率档位中，64 B 各 3k 最低通过档是 5M，8M 有更多吞吐余量；这只证明当前短接拓扑，不能替代外部线束、节点、终端电阻及温度范围的信号完整性验证，也不能将 10 秒/60 秒测试称为长期可靠性验收。所有负载档详见 [完整表](loads.md) 和 [逐方向统计](summary.json)。

## 方法与验收口径

使用 [专用板测应用](../../../Tests/Board/CANFDLoad/README.md)，六个 Release 镜像分别启动冻结 Classic、FD1M无BRS、FD2/4/5/8M有BRS。应用使用公开 SDK 头，私有硬件/Idle/扫栈诊断在独立 adapter；未改生产驱动、公共 API、生成配置、生产线程/栈/heap。保留正常 BMI/LSM/INS、温控、LIS/SPA、日志、USB 和监测服务；CAN2/4 为 Classic1M，无测试流量。应用额外一个 1 ms、优先级24、2048 B 的托管生产者，每次按档位提交 N 帧/方向，不补发错过的生产周期。

先检查双向、标准/扩展 ID、全部合法长度：Classic 36 帧、每个 FD 镜像 64 帧，共 **356/356**，每字节及实际 flags 正确。负载使用标准 ID 0x601（CAN1）、0x603（CAN3）；64 B 全部填充序号相关模式。这个 ID 安排在总线饱和时天然有利于 CAN1，单向饥饿不能被解释为公平分配。功能测试扩展 ID 通过不表示扩展 ID 的相同吞吐已验证；本次未测 Classic RTR。

生产前等待 INS VALID，各镜像实际等待 63 s，再稳定 3 s。每档记录实际窗口、生产周期、活动窗口接收与停止后的排空接收；软件 TTL=100 ms，结束后排空500 ms。负载窗口内不暂停调试器，不读取 CDC，不扫描任务栈；只在所有测试结束处设置硬件断点导出固定结构。CPU 用整窗 64 位 Idle runtime 与 uptime cycle 差值计算，包含 ISR、测试生产者和其余服务。窗口边界、1 ms 批次和短接线束限制了对微秒抖动/单帧最坏延迟的结论。

六镜像共54个窗口，负载提交接纳1704229帧、最终收到1683797帧；过载拒绝149771次、软件TTL过期20432帧（FD1M12659、FD2M7773），差额全部与TTL计数相符。其余接纳帧全部交付。所有镜像末档降回8 B各1k，均恢复完整交付；没有无法解释的接纳后丢帧或乱序/损坏。

阶段边界硬件读数：软件 RX 环溢出、硬件 RX 丢帧、Bus-Off、TEC、REC、ECR error-logging 全部为0，排空后TXBRP=0；ECR/PSR读取会影响累计/最近错误位，不能据此保证不存在未捕获的瞬态错误。未拔线或制造 ACK 错误/Bus-Off，因此没有验收故障恢复。当前计数与有效接收证明真实控制器间的外部收发/ACK路径，不是内部 HAL 回环模型。

## IMU、调度与内存

54个负载窗口内 BMI、LSM 样本以及 INS 发布计数均为1000/s（采样边界容差1条），INS missing/missed_deadlines、四传感器 error/overrun 均无增长。有效姿态标志保持；这不证明微秒抖动、姿态精度、温稳或四路 CAN＋UART 的期限。

LIS磁力计个别窗口仅68.2–99.6 Hz，其余100 Hz；低采样窗口在Classic空载和FD8M恢复档也出现，不能仅归因于CAN拥塞。SPA为16.0–16.5 Hz，未见错误。LIS严格100 Hz连续性仍待定位，不将双IMU/INS通过扩展为所有传感器均严格达标。

一次性栈水位在停止流量并排空后采集，六镜像最小剩余字节：CAN1/3 RX3092、TX3444，BMI1396、LSM1444、INS2096、SPI2480、I2C2676，负载生产者1476。实际工作线程优先级CAN1/2/3与BMI/LSM/INS=48、CAN4=47、SPI4=49、I2C4=46；UART未绑定，不能以此验收UART满载。各镜像应用对象池峰值2304 B、删除后0 B，分配/创建失败0，kernel heap最低空闲62080 B。仅为本测试应用水位，不授权缩减生产栈。

测试 ELF 链接 D1 292224 B、D2 6848 B、D3 544 B、ITCM 63104 B；额外结果/诊断缓冲在测试镜像内，这不是正常 Minimal 的内存占用。六 ELF布局与通知强符号检查通过，结构检查529项/31个HAL回调/49模块通过，六条测试应用编译命令只获得公共include。

## 非 CAN 问题

六镜像均 `RUNNING`、native_can_ready/usb_ready=true、parameter_load_status=0，但 `identity_load_status=SRNOS_STORAGE_CORRUPT (6)`；已用GDB直接读取确认，不只是主机结构解码推断。INS未携带UNCALIBRATED，不能据身份记录状态推断全部EEPROM或校准损坏。本次未执行任何EEPROM保存、校准或格式化，后续应只读定位身份分区/版本/CRC。

INS flags主要为545（VALID、WARMUP_TIMEOUT、RELATIVE_HEADING），部分窗口包含THERMAL_PENDING；这次只要求有效姿态后施加CAN负载，未证明温控收敛和持续温稳。

## 原固件恢复与证据

开始前确认CLion无调试会话、无其他GDB/J-Link控制者。J-Link V8.96、SWD4MHz、探针10114514，VTref3.30V。保存Flash地址0x08000000..0x080FFFFF完整1MiB，与 `build/itcm-migration-20260929/Release/SirinEmbedOS.elf` 的所有Flash LOAD段一致。每个测试镜像下载后 `compare-sections` 验证，再复位运行。

结束后用包含完整备份的单LOAD段镜像恢复整个Flash，读回1MiB逐字节一致；原/恢复 SHA256 均为 `b5731bd2d67c16d0b1ef66a50cf234b712d1b512d07b6f115209b6d350d08b2a`。切回原符号、复位运行，检查到Idle、HAL/RTOS tick已递增，CFSR/HFSR=0；清除测试断点、detach并退出本次GDB/J-Link。板上现在运行原固件，**没有把本次CAN优化/FD测试镜像留板**。

释放探针后通过CDC1只读采集8秒，tasks/ADC/cycles各9份报告、无协议解码错误，见 [metadata](restored-monitor/metadata.json) 与 [原始记录](restored-monitor/records.jsonl)。这里只证明原程序恢复运行及监测可用，不代表原程序具备本轮优化/FD行为。

[manifest.json](manifest.json)记录原/恢复Flash、测试ELF、结果bin、参与编译源码/公共私有头的SHA256及基线commit；源码为该基线上的未提交修改。六个原始bin与 [summary.json](summary.json)可用测试目录的decode/summarize工具离线复查。固定ARM结果ABI为11880 B；生产工具链使用短枚举，已用GDB/编译器布局核对，不按宿主机默认enum宽度解释。完整ELF、Flash备份、构建和J-Link日志保留在本机 `build/canfd-board-20261005/`，日期目录沿用启动测试时的10月5日。[GDB日志](gdb.log)、[结构检查](structure.log)和[结果复查](result-check.log)随本记录归档。

尚未覆盖CAN2/CAN4真实流量、MCP共享SPI压力、四路同时负载、UART混合期限、物理Bus-Off恢复、外部仪器位时序/信号完整性，以及长期/多节点/不同ID分配的负载。
