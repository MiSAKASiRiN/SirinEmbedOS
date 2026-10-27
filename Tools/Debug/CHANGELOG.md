# Changelog

## Unreleased

- 2026-10-24：移除历史传感器 GDB 转储脚本及栈转储解析脚本，保留 CubeMX 流程使用的 I2C4 时序检查。

- 2026-09-28：I2C 检查改读 Support/IO/i2c_timing.h，三处时序一致性检查通过；检查内容与硬件边界保持。

- 增加 I2C4 固定时序检查，覆盖 IOC/Glue 配置一致性及 Fast-mode 约束，避免只替换未经核对的寄存器常量。
- 完成 CubeMX CLI 生成后，将生成代码 `hi2c4.Init.Timing` 纳入一致性检查；当前 IOC/生成代码/Glue 均为 `0x10C7182D`，检查通过。

时序依据为 ST AN4235 的 TIMINGR 方程及 STM32H723 数据手册 DS13313 Rev 5 表 106。数字 SCL 采样不代替模拟边沿与电气时序验证。
