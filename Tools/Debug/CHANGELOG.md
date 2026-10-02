# Changelog

## Unreleased

- 2026-09-28：I2C 检查改读 Support/IO/i2c_timing.h，三处时序一致性检查通过；检查内容与硬件边界保持。 · 2026-09-28

- 增加 I2C4 固定时序检查，覆盖 IOC/Glue 配置一致性及 Fast-mode 约束，避免只替换未经核对的寄存器常量。
- 完成 CubeMX CLI 生成后，将生成代码 `hi2c4.Init.Timing` 纳入一致性检查；当前 IOC/生成代码/Glue 均为 `0x10C7182D`，检查通过。
