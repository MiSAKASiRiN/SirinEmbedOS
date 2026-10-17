# 验证记录

本文保存对应归档的原始报告。日期、版本、通过范围和结束状态仅对当时现场有效，不表示当前板上状态。当前阅读方法见本目录说明文件。

---

<!-- 原报告：README.md；原文 SHA256：fbe2f3e9b06a08d04e4177adfb7955e725ee16d79cbf6797eaac3422e9a41015 -->

# SRNOS 命名、公共边界与运行优化验证

2026-09-28，ST Arm Clang 21.1.1 / STM32CubeCLT 1.22.0。根 Debug/Release 均为 Minimal 配置。此次没有 Generate、烧录、重置或连接调试器；此前板测数据不能作为本轮改动的性能证明。

## 通过的检查

| 范围 | 结果与证据 |
|---|---|
| 能力回归 | 23 套主机测试全部通过，[汇总](host-tests.txt)。包含日志真实实现的耗尽/ISR/满队列、HostLink 活动表/长命令/断连/背压/锁外回调、持续 RX 公平处理、SPI/I2C 预留取消与事件竞争、Serial 总期限与回绕。 |
| 超时与消费者 | SPI/I2C/Serial 零等待、无限等待、非法范围及硬件内部期限通过；24C02 Blocking 与真实 I2C 分发器联合测试验证提前拒绝零等待、提交耗尽预算后取消确认，不提前交还在途 buffer。 |
| CAN4 恢复 | 真实 external TX backend + router 注入 FIFO 查询、二次查询、发送 SPI 失败及无后续 IRQ；有待发数据时按 1 ms 期限重试，持续失败受 TTL 清理，回绕通过。 |
| 公共接口 | 87 个公共头，151 个 C11/C++17 独立/聚合编译及传递依赖闭包；宏与 C/C++ AST 命名通过。[父项目检查](sdk-check.txt)。 |
| 真实父项目 | 13 条应用编译命令只获得 SDK 公共 include；`__has_include` 断言原生 RTOS、HAL、CMSIS-DSP 不可见。十种固件目标全部链接，包含 helper/manual、协议绑定、参数和 BringUp。 |
| 活跃命名 | 259 个自维护源码/头的文件名、宏、枚举与旧前缀检查；按编译数据库检查 110 个当前对象的全局符号。第三方及规定 ABI 按具体文件/名称列例外，无自维护整目录豁免。[结果](runtime-names.txt)。 |
| 结构与配置 | 453 项源码/工具合同、30 个唯一 HAL 回调、49 个来源模块、显式构建清单、统一托管入口、无整表暂停或默认扫栈通过。[结构结果](structure.txt)。 |
| 链接合同 | Debug/Release、第三方哈希、父项目 C++/TLS、IRQ/heap/D3、配置覆盖、MSP/libc 容量覆盖及越界拒绝均通过。Referee UI 保留原协议结构尺寸静态断言。 |

命令为 `python3 Tests/run_host_tests.py`、`python3 Tests/Build/check_sdk.py`、`python3 Tests/Build/check_runtime_names.py --compile-commands build/Debug/compile_commands.json`、`python3 Tests/Build/check_structure.py --compile-commands build/Debug/compile_commands.json`，以及 Debug/Release 的 CMake configure/build。

构建仅保留随板级 CMSIS 的 `optimize` 属性被 ST Arm Clang 忽略警告，未修改供应商头消除它。`git diff --check` 只剩工作区既有 Board/Core 的 CRLF 行提示（i2c.c、main.c）；本轮自维护修改无空白错误，生成文件原样保留。

## 内存与固定容量

| 链接区 | Debug | Release | 区容量 |
|---|---:|---:|---:|
| DTCM（含 heap/MSP 固定预留） | 131072 B | 131072 B | 128 KiB |
| D1（含 128 KiB heap 与 8 KiB libc arena） | 306944 B | 307008 B | 320 KiB |
| D2 | 9696 B | 9696 B | 32 KiB |
| D3 | 544 B | 544 B | 16 KiB |
| ITCM | 224 B | 192 B | 64 KiB |
| Flash | 268032 B | 190656 B | 1 MiB |

DTCM 的 100% 是链接预留覆盖到固定 MSP 末尾，并非运行期 heap 已耗尽。默认 D1 尚余 Debug 20736 B / Release 20672 B。实际 heap 最低余量和栈占用须上板测量，未缩减线程默认栈、队列或改变优先级/线程分离方案。

新增固定状态按目标 ARM ABI 核算，下面是对应对象增量，不把它们当成整个工作区固件的净差值：

- Serial：D1 增加 1032 B。6 槽（含 NONE）预留 12 份 80 B semaphore control block，另有状态/句柄；五个实际端口最多初始化 10 个静态状态信号量。当前 `SRNOS_SerialChannel` 为 1280 B。无新增 semaphore heap 分配。
- HostLink：相对已有 12 遥测槽基线，固定对象增加 378 B；相对旧 8 槽基线增加 586 B。context 4800 B（增加 128 B，其中活动表 10×12 B）；pending completion 144 B；遥测项 12×60 B（各增加 8 B）；registration 8 B、两个 bool 共 2 B。TX 槽仍 44 B、回放 4×520 B；交付通知复用原槽，不新增回调队列。链接间隙按最终 map 另计。
- CAN4：一个 context 增加 4 B 至 1048 B，失败标志复用 padding，新增 32 位期限；native/external 两份只读 backend ops 各增加一个指针（共 8 B 只读数据）。
- SPI：64 位毫秒换算期限使 8 个 request 各增加 8 B（合计 64 B），当前 `SRNOS_SPIRequest` 为 576 B。工作槽数量、512 B 事务容量及 DMA staging 不变。

RTOS heap 仍为 DTCM 65536 B + D1 131072 B；`SRNOS_SystemHeapBudget()` 的启动余量合同保持，明确上述 D1 静态状态不重复计入 heap。Minimal 自身 TLS 数据大小为 0；非零 TLS/线程控制区由父项目专用 fixture 检查。D3 `.bdma_buf` 起址 `0x38000000`，I2C staging 占 512 B；ADC 五个 halfword 位于 `0x38000200`，32 B 对齐，段末 `0x38000220`。专用段仍由启动显式初始化。详见 [资源数据](resources.json)、[Debug sections](debug-sections.txt)、[Release sections](release-sections.txt)。

## 保留的运行合同与板测

CAN RX/TX、HostComm/HostCmd、Indicators/SystemMonitor 继续分离。CAN 32 帧预算限定 worker；原生 FDCAN ISR 仍排空硬件 FIFO。CAN adapter 的 tick/poll_tx 保留 1 ms 合同，CAN4 RX 保留 5 ms 兜底；Indicators 仅在到期心跳遇 LED DMA busy 时短重试，启动三段 80 ms 提示音的既有忙等保留。I2C 取消/abort 的协作等待属于在途清理，不是常驻空闲轮询。

HostLink 完成重放仍为 4 项/2 秒；已执行业务在断连后不回滚。线协议、设备寄存器值、存储格式、ODR 和总线电气时序保持。源码 API、头路径、类型、超时参数及初始化可见性变化见 [迁移指南](../../SDKMigration.md)。

待板端以同配置、同负载比较 CPU、唤醒次数、最长临界区、INS/传感器/CAN 时序、CDC 断连/拥塞及异常恢复。循环 hook ≤1 µs、统计 CPU 增量 ≤1 个百分点、Monitor 平均 CPU ≤0.1%、监测临界区 ≤10 µs 尚未对本轮固件测量。仪表 ADC 精度和外部 CAN 实际负载验收同样未在本轮补做。

## 固件校验

- Debug：`19bb798acb5977711051142a3d7a8471b8281d88bb39c9a9271d40f79a02ef77`
- Release：`1f77906558c046427a2a22c6506a37031ad5a0f519220191943e18f996f5b36d`
