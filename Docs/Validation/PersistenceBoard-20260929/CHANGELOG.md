# 验证记录

本文保存对应归档的原始报告。日期、版本、通过范围和结束状态仅对当时现场有效，不表示当前板上状态。当前阅读方法见本目录说明文件。

---

<!-- 原报告：README.md；原文 SHA256：6d55a3e0fb1853cb1916686d0dcc915f83b986331e5e15c141471b7973ccdfaa -->

# 2026-09-29 EEPROM / HostLink / 静态 IMU 板端验收

结论：**部分通过，静态校准未通过，不能宣称完整验收。** 本轮按用户授权实际烧录，无 CubeMX Generate、第三方或生产算法改动。测试后原 EEPROM 全片恢复，最终运行最新 Minimal Release；原来的 EEPROM 本身没有有效校准，所以仍为未校准降级。

## 环境与证据

STM32H723VG，J-Link V8.96，探针 10114514，VTref 3.30 V，SWD 4 MHz。开始前 CLion 无调试会话、无其他 GDB/J-Link 进程或 CDC1 使用者。CDC1 为 `/dev/cu.usbmodem355C343932333`。初始板上旧固件仅支持旧命令，CONFIG 状态/校准查询返回 UNSUPPORTED_MESSAGE；先保存 1 MiB Flash，再用 EEPROMBringUp 只读备份 16 KiB EEPROM。

单独构建 `build/acceptance-20260929/maintenance`（Debug EEPROMBringUp）、`persistence`（Release Tests/Board/Persistence）、`minimal`（Release Minimal），不覆盖原构建产物。烧录均比对 `.text`，最终另比对向量表。测试窗口不设断点、不暂停；结束后才读取故障/资源状态。采集原始 CDC、JSONL、脚本与完整备份保留在该 build 目录；[artifacts.json](artifacts.json) 记录路径、长度与 SHA256，不把大二进制混入源码文档。

## 实测通过的范围

- 原 EEPROM 是此前留下的测试图样；启动识别损坏记录，不自动写默认值。父项目两项参数采用默认值，身份未配置，两颗 IMU 校准无效；INS 继续采样并标记 DEGRADED/UNCALIBRATED。
- CONFIG READ/WRITE/SAVE/LOAD：参数从默认 1/0 修改为 2.5/0.25，保存 revision=1，RAM 再修改为 3.5/0.5，LOAD 恢复 2.5/0.25。第二次保存相同内容 revision 仍为 1；含越界 ki=999 的批次整体拒绝，kp 未被部分修改。
- 软件复位后参数自动加载为 2.5/0.25，loaded=2、last_load=OK。**软件复位不等于真实断电试验。**
- SAVE 开始后查询到 busy，再 WRITE 6/0.6；保存快照为先前 5/0.5，完成时 current_generation=4、saved_generation=3、dirty=true。LOAD 恢复 5/0.5，证明不可变保存快照及代次合同。
- 测试后从 EEPROM 整片独立读回，业务记录 revision=2、正文32 B，主机校验记录 CRC 和提交页 CRC，并核对提交页重复字段。仅参数分区160字节改变；身份、两颗 IMU、保留区及扩展区逐字节未变。见 [介质验证](eeprom-validation.json)。
- 8 路 SOURCE V2 均接受 1000 Hz 订阅；25秒仅变量负载窗口每路收到22763条，约910.5条/s、合计7284条/s，**未达到主机完整接收8000条/s**。另外混合双IMU/磁场/气压负载约42.5秒，双IMU各约42500样本，无缺序、无CRC/解码错误；LATEST变量并不承诺历史逐点交付。
- 主机暂停读取2秒后 ECHO/INS查询恢复；正确携带hash的CANCEL_ALL后3秒无新增变量，关闭/重开CDC1后HELLO成功且订阅没有自动恢复。生产者与采样继续运行。未将OS串口缓冲视为已证明USB端点持续拥塞。
- I2C验收固件最终快照45456次事务全部完成，error/timeout/recover/回调溢出均0。四传感器状态READY，观察到的错误、overrun、recovery均0。INS组合遥测窗口missed_deadlines保持0、最大执行153 µs、最大周期1147 µs，存在短暂single/predicted轮次，不声称始终同步双源。

完整参数、遥测及状态原值见 [suite](suite-results.json)、[followup](followup-results.json)，统计汇总见 [metrics](metrics.json)。

## 阻塞项：静止被校准误拒绝

用户先确认刚移动过，随后明确已静止。重新采集后双IMU校准仍在约17 ms、每颗仅1个已接受样本后返回 `CALIBRATION_ERROR / MOVING`，saved_mask=0。GDB累积器显示BMI X轴前后约 +0.008522 / -0.011718 rad/s，峰峰值0.02024 rad/s，直接触发0.015限值。

静止复测原始样本：BMI12503条，三轴标准差0.00925/0.01015/0.01115 rad/s，峰峰值0.0714/0.0756/0.0852；LSM12499条，标准差0.00620/0.00902/0.00977 rad/s。当前工程标准差限值仅0.003 rad/s，不能适配本次静止原始噪声。加速度标准差约0.018–0.040 m/s²，均值姿态稳定。该证据支持当前静止判定与原始采样噪声不匹配，不能推断所有噪声来源已排除。

本轮没有临时放宽阈值、伪造零偏或写入单位校准；原IMU分区独立比对未变。下一步应核对带宽/单位与滤波口径，设计能区分噪声和运动的窗口判定，再用静止/运动反例重测；通过后才能测完整校准→保存→重启应用链路。见 [失败结果](retry-results.json) 和 [调试证据](gdb.log)。

注意：脚本在快速失败已完成2.5秒后重发同序号，超出2秒重放缓存而触发新job，符合合同；这**没有验证长校准活动请求的去重**。初次脚本还漏带CANCEL_ALL hash，被正确拒绝BAD_LENGTH，后续修正报文验证通过；不能把初次脚本退出码记为全套成功。

## 资源与开销

| 场景 | 整板平均CPU | Monitor平均CPU | Monitor最大临界区 |
|---|---:|---:|---:|
| 验收固件、监测订阅基线 | 20.97% | 0.04984% | 2 µs |
| 8路变量订阅 | 27.06% | 0.05151% | 2 µs |
| 变量+双IMU/环境流 | 31.19% | 0.05220% | 2 µs |

CPU为现有调度trace口径，包含归入任务的中断时间；基线仍有1ms变量生产者、测试管理线程和正常INS。不是纯空闲Minimal，也不是本轮统计开/关对照。Monitor两项目标在本窗口达标，完整loop hook≤1µs与统计增量≤1个百分点仍需单独测量。

验收固件heap_5当前空闲66024 B、历史最小61800 B；这是该测试配置运行值。最终Minimal静态D1占314240/327680 B，余13440 B，D2占9696 B，D3占544 B；D1已包含128KiB RTOS堆预留，不能把静态余量当作RTOS可分配量。基础ELF/TLS/DMA/向量检查通过，见 [检查日志](foundation-minimal.log)。本机GDB不支持Python，栈水位脚本未执行，**未取得本次HostCmd最小栈余量**，更未覆盖成功校准的最深保存调用链。

## 恢复与未覆盖项

新增EEPROMBringUp显式arm/hash保护的主机备份恢复命令3，仅测试代码。运行完成stage=8、restore_status=OK、restored=true，主机再次dump后与原16384字节完全一致，SHA256为 `7db3ccd567820a9ef9d177d8065dee54755a29d9a7e79a110be27f777380f971`。业务参数测试数据已清除。恢复的原图样不是有效生产参数/校准。

本轮没有发送CAN测试帧，没有依据历史短接假定当前接线。未做真实断电/提交页撕裂、身份工厂写入、成功校准与下次启动应用、长事务活动表压力、统计开关成本、串口或CAN组合压力、ADC仪表精度。最终正常固件继续运行，GDB与J-Link已退出，USB观察完成后释放串口。

最终释放探针后继续观察45秒：HELLO/ECHO成功，四传感器READY，INS已开始有效降级输出（未校准及温稳未保持），missed_deadlines=0、max_execution=32µs、max_period=1025µs。最终Minimal监测窗口平均CPU 19.769%、Monitor 0.04889%，ADC无错误/超时；本轮输入电压读数约0V，无仪表精度对照。见 [最终状态](final-results.json)、[监测汇总](final-monitor.json)。

本轮复核：EEPROM/Storage主机回归、结构检查及三个新构建产物的基础ELF检查通过，14篇受影响文档的120个本地链接目标存在。未重新运行完整25套主机矩阵，未修改生产逻辑；仅测试恢复入口有代码变更。编译保留上游CMSIS对Clang的既有optimize属性警告。
