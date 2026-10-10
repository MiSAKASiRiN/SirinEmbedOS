# 转台校准保存栈溢出修复 — 2026-10-10

## 结论

revision22 的转台保存请求在 HostComm 将延后工作入队时耗尽 2 KiB 栈，触发 `SRNOS_FAULT_REASON_KERNEL_STACK_OVERFLOW`，随后按默认策略复位。复现时 `storage.snapshot.write_count=0`，尚未开始 EEPROM 写入。revision23 去掉重复工作项副本、增加 HostComm 栈，已烧录并完成无运动的真实保存、重启加载和相同数据去重验证。

USART1 未绑定 Fault 紧急输出是当前默认设计；Fault 不主动配置或借用 USART1。保留区记录可经调试器读取，但断电会丢失。用户确认原异常后曾断电，本次诊断先保存无效的初始记录，再单独复现并捕获有效 Panic，而非把历史记录当作原异常现场。

## 复现与根因

原 ELF `build/IMUTurntable27/board/run24.elf`（revision22），板上 `.text` 比较匹配。J-Link V8.96 / 10114514、STM32H723VG、SWD 4 MHz、3.30 V，无其他探针会话。上电任务 job=0、phase=Idle，电机控制 bypass。

使用 Studio 已保存的真实任务 `830086713`（20261010-230944）的 26 个拟合系数，不重跑运动。在 GDB 暂停期间仅将 RAM 校准 job/records/phase 设置为该任务的 Result/24 条，`engaged=false` 始终不变；正常 HostLink 保存命令仍经过长度、阶段、系数范围检查。这个测试状态不写入固件，也未新增协议旁路。Panic 入口硬件断点捕获到：

- reason=4 / KERNEL_STACK_OVERFLOW，任务 `SRNOS_HostComm`，hooks.c:24。
- 栈范围 `[0x2000eb18,0x2000f314)`，PSP=`0x2000ea20`，已低于栈底 248 B；栈填充值余量 0，相邻 HostCmd TCB 也已被覆盖。
- `saving=1`，EEPROM write_count=0，尚未进入保存 worker。
- retained record version=2、size=328、sequence=1，离线重新计算 FNV checksum 通过。

原调用链含 RX 解码、项目处理器及 SubmitDeferred。入队又复制一个 280 B `work_item_t`，执行时同样按值复制。源码帧使用报告中 SubmitDeferred=600 B、command_step=472 B。

## 修复范围

- `HostLink.c` 入队直接设置调用者局部工作项的 session，由队列同步复制；执行借用 worker 当前持有的工作项，不再按值复制。请求/应答生命期和队列所有权不变。
- HostComm 栈 2048→3072 B；HostCmd 保持 2048 B。HostLink 启动 heap 预算 8→9 KiB。
- 固件 revision23。无新增线程，无优先级、线协议、EEPROM 格式、运动控制、生成文件、链接内存分区变化；Studio 插件无需重新安装。
- 新版帧使用 SubmitDeferred=320 B、command_step=192 B，分别减少 280 B。

## 验证结果

- HostLink 主机套件通过：请求/活动去重、会话、完成队列、背压、RX 预算、校准协议及部分保存状态；转台观测测试通过（CAN3 IDs、遥测、bypass、陈旧输出归零和 STOP）。
- 独立 Debug 构建 `build/IMUTurntableSaveFix`，烧录后 `.text` 匹配。FLASH=313088 B、D1=282912 B、D2=6848 B、D3=544 B、ITCM=63104 B；D1/ITCM 等静态用量未增加，额外 1 KiB 栈从现有 heap 分配。
- 同一真实系数请求在修复版得到 `<status,mask,storeBMI,storeLSM,revBMI,revLSM> = <0,3,0,0,3,3>`，约 0.256 s 收到响应；write_count=2、skipped_writes=0。任务 Saving→Done，双记录实际 CRC/提交完成。
- 保存期间并行订阅 32 个电机标量（每项最高 200 Hz）及 BMI/LSM/INS 三条 1 kHz 流，持续 12 s 状态轮询；BMI/LSM 各收到 12000 样本，此时 INS 尚在启动准备阶段，未收到姿态样本，不能算三流满负载测试。无解码错误、无新增 Panic，三个电机输出始终为 0。
- 受控重启后双记录 load_status=OK、active_revision=3、model_version=2；读出 active 结构的 26 个 float32 与 Studio 拟合发送值逐位一致。
- 重新单独提交相同值，成功应答仍为双 revision3；skipped_writes=2。此轮栈填充值余量 HostComm=1096 B、HostCmd=372 B；这是重启后相同值保存路径的实测余量，不代表全写入路径或任意父项目回调上界。
- 最终温稳后 INS phase=6、healthy_mask=3，独立订阅 3 s 收到 3000 个姿态样本且无解码错误，见 `final-ins.json`。
- 最后清除测试状态并受控重启，5 s 观测 revision23/Idle、fault=0、三轴 voltage_raw=0。所有本次硬件断点删除，MCU 运行，GDB/J-Link 退出，CDC1 释放。

## 验证边界与证据

用户已反馈运动校准和取消保持正常，本次只复现和验证保存路径，不宣称重新跑过完整运动校准。无更改或重新安装 Studio。实际写入的是用户最近一次拟合结果，已在重启后激活。

`original-gdb.log` 为栈溢出现场；`fault-confirmed.bin` 是校验有效的 retained 记录；`fixed-gdb.log` 包含烧录、保存状态、重启加载和栈地址；`save-result.json`/`summary.json` 为并行遥测保存结果；`coefficients-verified.json` 为 float32 比较结果；`stacks-unchanged-save.json` 明确采集路径；`final-idle.json` 为结束状态。完整构建日志、原始串口包及栈二进制保留在 `build/IMUTurntableSaveFault/`，ELF/source 身份见 `sha256.json`。
