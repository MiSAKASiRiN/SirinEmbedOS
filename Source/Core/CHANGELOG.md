# Changelog

## Unreleased

- 2026-10-10：默认致命 Panic/Fault 输出至 USART1 PB14、115200/8N1；正常启动不占用，保留启动期自定义覆盖。板级 HSI/发送轮询有界且失败锁存，不依赖 RTOS/HAL/DMA。Core 主机测试及五个寄存器/超时场景通过，revision24 固件编译、烧录、USART1 实际 ExplicitPanic 报告和复位验证完成；不是所有硬件故障下输出的保证。

- 2026-09-29：增加 SRNOS_ITCM_BSS 与启动期 64 位清零/复制；代码/BSS/Watch 共同校验 ITCM 上界及对齐。MpuDcache Watch 增加 code/BSS 字节数（记录 64→72 B）；DMA/Fault 格式不变。边界/复位主机测试、最终反汇编与板端填充后软件复位验证通过；真断电及 CAN 负载待验。

- 将 HostLink CRC32 提取为独立 Core 增量/一次性接口；Storage 共用，CRC 向量与原 Codec 全套主机测试通过，线协议不变。

- 2026-09-28：公共基础操作继续使用 SRNOS 命名；配置头统一为 `SirinEmbedOS/Config.h`，段放置宏、版本宏和自维护配置宏统一 `SRNOS_*`。链接器约定符号、libc/RTOS 固定入口属于明确 ABI 例外。父应用不再从私有 Config/Board/Kernel 搜索路径取得依赖。验证范围为主机与构建；未重新烧录。

- 2026-09-28：清除 MemorySections 旧别名与 Version/头保护旧名，配置宏统一 SRNOS_CONFIG_HEADER；FreeRTOS trace 声明转为私有。

- 2026-09-28：SystemMonitor替代Diagnostics线程，1 Hz ADC/任务统计与HostLink发布；增加ADC板级BDMA接入、逐槽统计和主机工具，默认取消扫栈。Indicators/生成文件不变。验证范围为主机与构建，未烧录/板测。

- 板级早期启动、Fault/libc 实现在 Board/Support/System；Core 公共操作及内存段合同不变。验证范围为主机与构建；未板测。
- 2026-09-29：Watch 迁出 D2 后 retained 地址随布局调整，不承诺跨本次固件升级保留；同一固件的软件复位保留合同不变。
- libc arena 独立管理 `_sbrk/sbrk`，不再跨 DTCM/D1 比较地址；负增量、溢出及耗尽返回失败。当前内存布局见 [README](README.md#链接布局)。
