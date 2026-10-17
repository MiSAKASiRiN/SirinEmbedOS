# Changelog

## Unreleased

- 2026-10-10：默认致命 Panic/Fault 输出至 USART1 PB14、115200/8N1；正常启动不占用，保留启动期自定义覆盖。板级 HSI/发送轮询有界且失败锁存，不依赖 RTOS/HAL/DMA。Core 主机测试及五个寄存器/超时场景通过，revision24 固件编译、烧录、USART1 实际 ExplicitPanic 报告和复位验证完成；不是所有硬件故障下输出的保证。见 Docs/Validation/IMUTurntableLatency-20261010。

- 2026-09-29：增加 SRNOS_ITCM_BSS 与启动期 64 位清零/复制；代码/BSS/Watch 共同校验 ITCM 上界及对齐。MpuDcache Watch 增加 code/BSS 字节数（记录 size 72）；DMA/Fault 格式不变。边界/复位主机测试、最终反汇编与板端填充后软件复位验证通过；真断电及 CAN 负载待验，见 [记录](../../Docs/Validation/ITCM-20260929/README.md)。

- 将 HostLink CRC32 提取为独立 Core 增量/一次性接口；Storage 共用，CRC 向量与原 Codec 全套主机测试通过，线协议不变。

- 2026-09-28：公共基础操作继续使用 SRNOS 命名；配置头统一为 `SirinEmbedOS/Config.h`，段放置宏、版本宏和自维护配置宏统一 `SRNOS_*`。链接器约定符号、libc/RTOS 固定入口属于明确 ABI 例外。父应用不再从私有 Config/Board/Kernel 搜索路径取得依赖。 本轮主机/构建范围见 `Docs/Validation/SRNOSBoundary-20260928/README.md`；未重新烧录。

- 2026-09-28：清除 MemorySections 旧别名与 Version/头保护旧名，配置宏统一 SRNOS_CONFIG_HEADER；FreeRTOS trace 声明转为私有。

- 2026-09-28：SystemMonitor替代Diagnostics线程，1 Hz ADC/任务统计与HostLink发布；增加ADC板级BDMA接入、逐槽统计和主机工具，默认取消扫栈。迁移system_monitor_enabled及HostLinkPrepareEx合同；Indicators/生成文件不变。主机与构建验证见Docs/Validation.md，未烧录/板测。


- 板级早期启动、Fault/libc 实现在 Board/Support/System；Core 公共操作及内存段合同不变。 本次主机及构建验证见 Docs/Validation/BoardSupport-20260928/README.md；未板测。

## 2026-10-17 文档整理

- README 仅说明当前接口、使用方式和边界，补充接入示例；历史结果移至本文件，保留证据链接。此项不修改实现，不新增板端验证。

### 从 README 归档的历史记录

2026-09-29 容量迁移、主机/构建和板端范围见 [ITCM 验证记录](../../Docs/Validation/ITCM-20260929/README.md)。

物理串口 Panic 输出与主机超时测试见 [2026-10-10 记录](../../Docs/Validation/IMUTurntableLatency-20261010/README.md)。

### 补充归档的原始说明

以下为原 README 的历史描述，保留当时的验证口径，不代表本次重新验证。

应用仍可在配置阶段调用 `SRNOS_Platform_ReserveEmergencyOutput` 一次覆盖默认输出，系统配置冻结后拒绝注册；自定义回调须在关中断状态下有界返回，不能等待 DMA、RTOS 或动态内存。注册自定义 writer 后不再接管 USART1。正常日志不可调用紧急通路。物理串口 Panic 输出与主机超时测试见 [2026-10-10 记录](../../Docs/Validation/IMUTurntableLatency-20261010/README.md)。

- DTCM/D1 heap 默认 64 KiB/128 KiB，由 SDK 配置头控制；不能当 DMA 分配器使用。
- MSP/libc arena 默认各 8 KiB，可用链接符号 `SRNOS_MSP_BYTES` / `SRNOS_LIBC_BYTES` 覆盖；它们不是 RTOS heap 的别名。
- `.dma_nc`、`.dma_nc_buf`、`.eth_dma` 在非缓存 D2，`.bdma_buf` 在 BDMA 可达的 D3。均为 NOLOAD，启动时显式清零。
- `SRNOS_ITCM_BSS` 将仅 CPU 访问、无非零初值的状态放入 `.itcm_bss`；当前为 CAN Router 上下文。`.debug_watch` 也位于 ITCM，按符号读取。二者与 `.itcm_text` 共享 64 KiB，总容量由链接器检查。
- `.dtcm_bss` 启动清零；ITCM BSS/Watch 使用对齐的 64 位 volatile 写入清零，`.itcm_text` 从对齐 Flash 源按 64 位复制，避免首次写 ITCM ECC 字时读取旧内容。反汇编验证为 STRD；两块 heap payload 不保证清零。
- 专用段初始化发生在 C++ 全局构造之后、SDK 配置之前。构造期注册表和带初值对象不能直接放进 ITCM BSS；不把通用 heap、DMA staging 或 Fault retained 搬入 ITCM。
- `SRNOS_DbgMpuDcache` 末尾增加 `itcm_text_size` / `itcm_bss_size`，记录由 64 B 扩展为 72 B，使用 header.size 识别；HostLink/Fault 格式不变。
- `.sirinos_noinit` 在非缓存 D2，启动不清零，Fault 通过 magic/version/size/checksum 判断有效性。掉电后不保证保留。本次移出 Watch 后 retained 地址随 D2 重排变化，不承诺跨此次固件升级保留旧记录；同一固件软件复位的保留合同不变。
- `_sbrk/sbrk` 仅在独立 libc arena 中移动，不再比较 DTCM `_estack` 与 D1 地址。负增量、溢出和耗尽返回失败。
