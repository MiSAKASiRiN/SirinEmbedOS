# 托管任务与循环统计验证（2026-09-28）

本轮在已有 SystemMonitor 与 Board/Support 工作区上继续实现。未连接/操作调试器，未 Generate、烧录、提交或推送；使用独立 build/ManagedTasks-* 目录，不覆盖原调试 ELF。Board/Core、Support、Linker 及 IOC 与本轮前备份一致；FreeRTOS/USB 上游哈希通过。产物 SHA256 和内存符号见 summary.json。

## 已执行

- 22 组主机套件通过：host-tests.log。Kernel 使用 pthread 假 RTOS 调度实际 thread/object_pool/sync 实现及 UBSan；生命周期覆盖创建、描述/名称复制、启动屏障前停止、init 失败及等待中停止、step 失败/正常结束、自身 Join/Delete 拒绝、清理先于回收、删除超时、并发 Join、句柄复用、容量耗尽和 SDK 所属保护。不是 Cortex-M port 的板端证明。
- 时间/同步覆盖 1 ms 周期、严格未来截止点、长延迟跳过、tick 回绕、DelayUntil 逾期、Delay 取消和 Yield；线程通知位竞争、ISR 优先级限制、持锁/等待者阻止删除、递归锁、信号量/队列/事件。假内核不证明目标机优先级继承和 PendSV 时序。
- 循环统计注入 DWT 时间，核对墙钟 min/mean/max、启动延迟、有符号与绝对抖动、截止/跳过、实际窗口、事件不适用、空窗口、生命周期重置及计数回绕。成本数值不是板端基准。
- SystemMonitor 延续 ADC 换算与恢复/隔离测试，并验证新增 112 B 循环记录的 C/Python 一致性、负抖动、窗口/年龄、三流节流/拥塞/取消、64 槽每批四条的 16 次轮转及旧双流固件兼容。
- Minimal Debug/Release、EEPROMBringUp Debug 链接通过。九个父项目固件（含其余示例、C++/TLS/全组件）和 MSP/libc 覆盖/越界拒绝通过。父项目预算 fixture 的非默认 DTCM heap 改为 48 KiB，以容纳新增池和统计预留；SDK 默认仍 64 KiB。实际 TLS 创建/删除压力须板测。
- 结构检查覆盖 420 份源/头/工具、30 个唯一 HAL 回调、49 个来源模块及显式源码覆盖；SDK 原生线程创建/删除只在 Kernel/thread.c，业务示例无直接 RTOS 头（EEPROM 内核专项适配单列），公共 Thread/Sync 不泄漏原生类型。Monitor/Runtime 无整表暂停调度/栈扫描。
- 三份 ELF 的内存、TLS、libc arena、retained 和异常向量通过；ADC buffer 位于 D3 0x38000200，32 字节对齐，D3 总 544 B。

## 内存与执行边界

| 构建 | D1 已预留/占用 | D1 未预留余量 | D2 | D3 | Flash |
|---|---:|---:|---:|---:|---:|
| Minimal Debug | 305472 B | 22208 B | 9696 B | 544 B | 263880 B |
| Minimal Release | 305504 B | 22176 B | 9696 B | 544 B | 186816 B |
| EEPROMBringUp Debug | 321952 B | 5728 B | 11552 B | 544 B | 267896 B |

D1 占用包含 128 KiB heap 和 8 KiB libc arena，不能当作实际堆使用量；DTCM 链接使用 128 KiB 包括热数据/heap/MSP 的预留。EEPROM 示例另有 16 KiB 备份，静态余量较小。实际运行余量通过 KernelGetResources 验收。

父项目池默认启动预留 32 KiB，运行期按 256 B 块使用静态 RTOS 创建接口，资源耗尽返回错误。固定生命周期登记表约 6200 B、循环累加器 7168 B 改为调度前一次预留，保守启动预算各 8 KiB，运行阶段不扩容；循环开关关闭不分配累加器。若把两者留在 D1 BSS，EEPROM 示例会越界，现已在原有 heap 分区内明确预算，链接脚本与 DMA 区未改。

所有 SDK worker 栈/优先级和等待语义经本轮前备份对照：CAN 的事件/有界排空、SPI 的通知/1 tick、I2C 的活动超时/空闲无限等待、Serial 通知/1 tick、RS485 队列与事务等待、Log 队列 10 ms、HostCmd 队列、HostComm 通知；Sensors/INS 保留超期重置，Indicators/RC/Referee 保留相对延时。普通等待在 hook 外，处理内 DMA/I/O 仍在墙钟内。Idle/Timer 保留上游循环，仅配置名称。

## 板端尚未执行

- hook 开始+结束无抢占基准 ≤1 µs、代表负载新增统计 CPU ≤1%；Monitor 稳定平均 CPU ≤0.1%、采样临界区 ≤10 µs。遥测 hook_cost 是含潜在抢占且省略最终记账的估计，不代替这些基准。
- 相同负载下统计开/关比较 INS、CAN、两颗 IMU 和传感器时序；检查原栈预算在新包装层下的极端路径。默认不扫栈，保留溢出检测/Fault。
- 反复运行期创建/停止/删除、Picolibc TLS、同步竞争与 ISR 压力；真实 port 的退出挂起/删除顺序及有限 I/O 超时。
- CDC 断连/拥塞、ADC 故障与停止隔离；仪表核对电流/输入电压和内部通道。此前硬件记录不能证明本轮更改后的固件已经通过。

已有 Cube CMSIS 的 unknown optimize attribute 构建警告保留；git diff --check 只报告原有 Board/Core 的 CRLF 行尾，未为消除报告改动生成文件。
