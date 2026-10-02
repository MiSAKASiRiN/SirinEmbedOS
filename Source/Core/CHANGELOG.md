# Changelog

## Unreleased

- 2026-09-29：增加 SRNOS_ITCM_BSS 与启动期 64 位清零/复制；代码/BSS/Watch 共同校验 ITCM 上界及对齐。MpuDcache Watch 增加 code/BSS 字节数（记录 size 72）；DMA/Fault 格式不变。边界/复位主机测试、最终反汇编与板端填充后软件复位验证通过；真断电及 CAN 负载待验，见 [记录](../../Docs/Validation/ITCM-20260929/README.md)。

- 将 HostLink CRC32 提取为独立 Core 增量/一次性接口；Storage 共用，CRC 向量与原 Codec 全套主机测试通过，线协议不变。

- 2026-09-28：公共基础操作继续使用 SRNOS 命名；配置头统一为 `SirinEmbedOS/Config.h`，段放置宏、版本宏和自维护配置宏统一 `SRNOS_*`。链接器约定符号、libc/RTOS 固定入口属于明确 ABI 例外。父应用不再从私有 Config/Board/Kernel 搜索路径取得依赖。 本轮主机/构建范围见 `Docs/Validation/SRNOSBoundary-20260928/README.md`；未重新烧录。

- 2026-09-28：清除 MemorySections 旧别名与 Version/头保护旧名，配置宏统一 SRNOS_CONFIG_HEADER；FreeRTOS trace 声明转为私有。

- 2026-09-28：SystemMonitor替代Diagnostics线程，1 Hz ADC/任务统计与HostLink发布；增加ADC板级BDMA接入、逐槽统计和主机工具，默认取消扫栈。迁移system_monitor_enabled及HostLinkPrepareEx合同；Indicators/生成文件不变。主机与构建验证见Docs/Validation.md，未烧录/板测。


- 板级早期启动、Fault/libc 实现在 Board/Support/System；Core 公共操作及内存段合同不变。 本次主机及构建验证见 Docs/Validation/BoardSupport-20260928/README.md；未板测。
