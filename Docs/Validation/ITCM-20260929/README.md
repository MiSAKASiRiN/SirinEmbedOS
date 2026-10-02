# 2026-09-29 CAN Router / Debug Watch 迁入 ITCM

用户以释放 RAM 为目标，要求正式迁移 CAN Router 和 Debug Watch，并提供在线 PCB。两者优先使用 ITCM；上一轮 Monitor/IMU/INS 缓存方案不与本方案叠加。未迁移新的热点代码，未缩减线程、栈、队列或路由容量，未扩大 RTOS heap。IOC、生成代码、HAL/第三方、线协议和 EEPROM 内容未修改。

## 实现与启动合同

- `SRNOS_ITCM_BSS` 将 CAN Router `s_ctx` 的 59920 B 软件上下文放入 `.itcm_bss`；包含通道路由、adapter、one-shot 队列、group shadow、发送暂存与统计。
- `.debug_watch` 从 D2 移入 ITCM；CPU 与调试器访问不经过 D-cache。普通 Referee 调试变量仍是原 D1 BSS，不因名称含 Debug 自动迁移。
- `SRNOS_MemInitEarly()` 在 main 早期入口、MPU/cache 与 SDK 配置之前，以对齐 64 位 volatile 写入清零 ITCM BSS/Watch，按 64 位复制 ITCM code。Release 反汇编确认为 STRD/LDRD；不在初次 ITCM ECC 写入时依赖 byte memset。
- ITCM Flash load 源对齐 32 B，代码/BSS/Watch 分离、边界对齐且整体不得超过 64 KiB；数据不得位于 NULL 地址。NOLOAD BSS 禁止非零静态初值。
- 专用段初始化在 C++ 全局构造之后，构造函数不能访问这些对象。遥测/参数构造期注册表仍留在普通 BSS。
- `SRNOS_DbgMpuDcache` 末尾新增 ITCM code/BSS 字节数，header.size 从 64 B 更新为 72 B，其余既有记录和 HostLink/Fault 格式保持不变。
- DMA 描述符和 staging 留在 D2，ADC/I2C BDMA 留在 D3，原生 CAN RX ring 留在 DTCM。Fault retained 留在 D2 且不清零；移出 Watch 后地址重排，不承诺跨这次固件升级保留旧故障记录，同一固件复位保留合同不变。

## 当前内存预算

基线为 `build/static-threshold-20260929/Release/SirinEmbedOS.elf`，产物为 `build/itcm-migration-20260929/{Debug,Release}`。数字为链接静态占用，包括预留 heap，不是 heap 当前已分配量。

| 区域 | Release 迁移前 | Release 迁移后 | 迁移后空余 |
|---|---:|---:|---:|
| D1 / 320 KiB | 314240 B | **254336 B** | **73344 B / 71.625 KiB** |
| D2 / 32 KiB | 9696 B | **6848 B** | **25920 B / 25.313 KiB** |
| D3 / 16 KiB | 544 B | 544 B | 15840 B |
| ITCM / 64 KiB | 192 B | **63008 B** | **2528 B / 2.469 KiB** |
| Flash / 1 MiB | 200528 B | 200608 B | 847968 B |

Release 实际释放 D1 **59904 B（58.5 KiB）**、D2 **2848 B（2.781 KiB）**。符号大小与静态释放量的微小差异来自对齐。新增两个 Watch 字段使 ITCM Watch 总段从原 2848 B 扩到 2880 B。

Release ITCM：代码 `[0x0000,0x00C0)` 192 B；Router BSS `[0x00C0,0xEAE0)` 59936 B（含 16 B 尾部对齐）；Watch `[0xEAE0,0xF620)` 2880 B。Debug ITCM 63040 B、D1 254272 B、D2 6848 B、D3 544 B、Flash 280928 B。

DTCM 仍为 heap 65536 B + CAN RX ring 段 41568 B + MSP 8192 B，中间空隙 15776 B。链接摘要的 100% 是 MSP 到达银行末端；该空隙没有自动进入 heap。

RTOS heap 仍为 DTCM 64 KiB + D1 128 KiB。板端 Release 运行 184173 tick 时当前空闲 **66024 B**、最低空闲 **61800 B**；本次释放的 D1 空间没有自动变成 RTOS 可分配量。增大 heap 必须另行调整预算。

## 主机与构建

- [25 组主机套件](host-tests.log)通过，Core 新增 64 位初始化的边界、空区间、复制和重复清零测试；路由与实际 CAN worker 回归保持通过。
- [SDK 接入](sdk.log)：91 个公共头、独立 C11/C++17 与依赖闭包、父应用 include 隔离、十个固件目标、MSP/libc 配置覆盖、越界拒绝及第三方哈希通过。
- [结构](structure.log)、[公共命名](public-names.log)通过；新链接导出符号使用 `SRNOS_ITCMBssStart` / `SRNOS_ITCMBssEnd`。
- Debug/Release 完整构建和 foundation ELF 检查通过：CAN 上下文/Watch ITCM 归属、NOBITS、对齐、共享容量、DMA/BDMA/Fault 银行、TLS、向量与 CAN 强通知符号。
- [产物 SHA256](artifacts.json)、[Debug 链接摘要](build-debug.log)、[Release 链接摘要](build-release.log)。构建和 ELF 检查不能替代板端实际访存验证。

## 板端验证

操作前 CLion 无调试会话，未发现其他 GDB/J-Link 或 CDC1 占用。使用 J-Link V8.96，探针 10114514，STM32H723VG，VTref 3.30 V，SWD 4 MHz；先备份原 Flash 至 build 目录，再烧录匹配 Debug，最终烧录匹配 Release。Flash `.text`、向量与 ITCM load 段 compare-sections 匹配。

**初始化与软件复位：** 在 `SRNOS_Platform_Initialize()` 入口硬件断点暂停，此时专用段初始化已完成、应用注册尚未开始。向完整 CAN BSS + Watch 区域写 `0xA5` 后软件复位。读回验证 59936 B CAN BSS 全为 0；Watch 除重新发布的内存记录外全为 0；224 B Debug ITCM code 与 Flash 源逐字节一致。内存 Watch init_count=1、size=72、容量字段正确，CFSR/HFSR 均为 0。见 [断言结果](startup-checks.json) 与 [GDB 日志](gdb.log)。这不是物理断电/ECC 故障注入试验。

**Watch 与服务运行：** Debug 运行约 18.7 s 后，SPI 99928 次开始/完成相等，I2C 7751 次开始/完成相等，error/timeout/recovery 为 0，双 CDC 已配置。Release 的 ITCM Watch 原始 dump 按匹配 ELF 地址解析：12 条已初始化记录头有效；未使用的 MCP/DR16/VT03 槽保留零值。既有 ParametersFreeze 实现清零记录后只填计数、不设置 Watch header，本轮未顺带修改；其 registered=0、capacity=32 与 Minimal 配置一致，不能宣称所有 Watch header 都已完善。见 [Watch 摘要](watch-summary.json)。

**Release 连续运行：** 20 s CDC1 监测窗口三条流各 21 帧，解码错误为 0；ADC error/timeout/recovery、Monitor skipped 均为 0，任务数 22。整板 CPU 平均 19.487%，Monitor CPU 平均 0.04878%、最大 0.049%，监测临界区最大 2 µs。未进行同负载开关对照，不由此宣称加速或完整实时性验收。见 [监测元数据](monitor-metadata.json)、[摘要](monitor-summary.json)。

随后 25 s 五流采集，在最后一组完整状态报告中：BMI 24008、LSM 24007、INS 24008、磁场 2359、气压 392 条，均无缺序/重复，队列 overflow/failed=0；后续不足一秒的尾段不计入该完整报告。双 IMU healthy_mask=3，四传感器均 READY，INS 保持 VALID、UNCALIBRATED 清除、missed_deadlines=0。末次 phase=DEGRADED、flags=561，仍有 THERMAL_PENDING 和历史 WARMUP_TIMEOUT，不能把内存迁移完成写成温稳或姿态精度已验收。见 [传感器摘要](sensor-summary.json)。

## 最终状态与未测项

最终板上保留本次 **Minimal Release**，目标继续运行；本次硬件断点已删除，GDB 已 detach/退出，J-Link Server 已退出，CDC1 已释放。没有执行参数 SAVE、校准或 EEPROM 写入；原校准继续由启动加载使用。释放探针后再次 CDC 采集 3 秒，三条监测流均有有效报告、解码错误为零，见 [最终连通性](final-monitor-metadata.json)。

用户确认当前 CAN1/CAN3 未短接，未发送外部 CAN 压力流量；真实收发吞吐、组合负载、物理断电、长期稳定性及统计 hook 开关成本未在本次验证。ITCM 仅余约 2.5 KiB，增加路由/Watch 容量必须重新检查所有目标 map，不叠加此前 31.8 KiB 缓存方案。

原始内存 dump、Flash 备份、CDC JSONL、源码对应 ELF/map 与完整构建输出留在 `build/itcm-migration-20260929/`；本目录保存摘要、关键日志与哈希。未提交或推送。
