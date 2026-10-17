# Changelog

## Unreleased

- 2026-09-28：I2C 检查改读 Support/IO/i2c_timing.h，三处时序一致性检查通过；检查内容与硬件边界保持。 · 2026-09-28

- 增加 I2C4 固定时序检查，覆盖 IOC/Glue 配置一致性及 Fast-mode 约束，避免只替换未经核对的寄存器常量。
- 完成 CubeMX CLI 生成后，将生成代码 `hi2c4.Init.Timing` 纳入一致性检查；当前 IOC/生成代码/Glue 均为 `0x10C7182D`，检查通过。

## 2026-10-17 文档整理

- 使用说明保留当前合同、入口和示例；日期战报与修改记录归入本文件。仅调整 Markdown，代码、原始测量数据和第三方源码未改动。

### README.md 中保留的历史说明

以下内容描述原文所述时期，不作为当前用法；当前入口见 [README.md](README.md)。

时序依据为 ST AN4235 的 TIMINGR 方程及 STM32H723 数据手册 DS13313 Rev 5 表 106。实际板测和数字 SCL 采样见 `Docs/Validation/I2C400-20260928`。
