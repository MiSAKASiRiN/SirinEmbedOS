# Changelog

## Unreleased

- 2026-09-29：同步说明早期初始化的 ITCM BSS/Watch 合同，保留现有 System 入口及生成边界；Core 完成 64 位初始化。构建、软件复位与实板 Watch 检查见 [记录](../../Docs/Validation/ITCM-20260929/README.md)。

- 2026-09-28：配合公共依赖隔离，SPI/I2C/CAN、WS2812/BMI/MCP 等板级装配引用私有头；自维护跨文件符号统一 SRNOS，CubeMX 与 HAL 规定入口保留，未改变固定硬件时序或内存段。验证边界见本轮总记录；没有 Generate 或烧录。

- 2026-09-28：修复 HAL normal BDMA 完成后 EN 保留导致 ADC 首轮误隔离；停止 ADC 后显式关闭独占通道并核验，新增真实 READY/EN 行为回归。正常采集及软件故障路径已板测，物理卡死未测。

- 2026-09-28：SystemMonitor替代Diagnostics线程，1 Hz ADC/任务统计与HostLink发布；增加ADC板级BDMA接入、逐槽统计和主机工具，默认取消扫栈。迁移system_monitor_enabled及HostLinkPrepareEx合同；Indicators/生成文件不变。主机与构建验证见Docs/Validation.md，未烧录/板测。


- 2026-09-28：板级支撑整理为 Support/System、IO、Devices、USB；指示器策略归服务，USB 准备和 MCP 默认配置收拢，蜂鸣器/WS2812 固定硬件宏私有化。保留生成边界及现有时序/调度/内存合同；21 组主机、九个父项目目标、Minimal/Board/EEPROMBringUp 构建通过，未生成/烧录/板测。详见 Docs/Validation/BoardSupport-20260928/README.md。 · 2026-09-28

- I2C4 增加固定 Fast-mode 时序与启动检查，IOC 同步为 `0x10C7182D`；仅在生成文件 USER CODE 添加调用，未执行 CubeMX regenerate。
- 时钟校验使用 PLL3R 专用查询，避开当前 HAL 通用频率查询不支持 I2C4 的情况；不改第三方 HAL、引脚、DMA、IRQ 或内存布局。
- Debug/Release 和父项目构建通过；硬件 TIMINGR 读回一致，数字 SCL 约 370 kHz，EEPROM/LIS/SPA 并发 21 项验证通过、零总线错误。模拟边沿尚未测量，详见 Docs/Validation/I2C400-20260928。
- 后续完成 CubeMX CLI 生成，生成的 TIMING 与 Glue 常量一致，USER CODE 入口完整保留；本轮未改变 Glue 运行行为，未重新烧录。
