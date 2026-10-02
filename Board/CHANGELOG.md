# Changelog

## Unreleased

- 2026-09-29：自维护 linker 增加 ITCM BSS，将 Watch 移入 ITCM，检查代码/BSS/Watch 合并容量与 64 位初始化对齐；D2 Fault retained 随段重排，但清零排除合同不变。生成源码、IOC/HAL 未改。Debug/Release、父项目及板端范围见 [记录](../Docs/Validation/ITCM-20260929/README.md)。

- 2026-09-28：SystemMonitor替代Diagnostics线程，1 Hz ADC/任务统计与HostLink发布；增加ADC板级BDMA接入、逐槽统计和主机工具，默认取消扫栈。迁移system_monitor_enabled及HostLinkPrepareEx合同；Indicators/生成文件不变。主机与构建验证见Docs/Validation.md，未烧录/板测。


- 2026-09-28：板级支撑整理为 Support/System、IO、Devices、USB；指示器策略归服务，USB 准备和 MCP 默认配置收拢，蜂鸣器/WS2812 固定硬件宏私有化。保留生成边界及现有时序/调度/内存合同；21 组主机、九个父项目目标、Minimal/Board/EEPROMBringUp 构建通过，未生成/烧录/板测。详见 Docs/Validation/BoardSupport-20260928/README.md。 · 2026-09-28

- 新增可执行 `generate.py`，支持指定 CubeMX、超时与 dry-run；系统调用前备份 Board，保存脚本/日志/变化清单，成功后检查 IOC/生成代码/Glue 时序。默认不构建、不烧录。
- 首次相对 IOC 路径触发 CubeMX 找不到文件及错误窗口，Board 未改动；改为动态带引号绝对路径后，从 `/tmp` 调用实测生成和时序检查通过，Board 内容无变化。AGENTS.md 与 README 已记录该约束。
- 使用 STM32CubeMX 6.18.1 `-q Board/generate.mx` 执行 `project generate`，退出码 0；生成的 I2C4 TIMING 更新为已板测的 `0x10C7182D`。
- 与生成前完整 Board 快照比较，仅 `Core/Src/i2c.c` 的时序赋值发生内容变化；USER CODE、自维护 CMake、HAL/CMSIS、IRQ 和 IOC 保持一致。生成日志及构建验证见 Docs/Validation/CubeMX-20260928，未烧录或操作板端调试会话。
- 生成后 Minimal Debug/Release、九个父项目构建及 IOC/生成代码/Glue 三处时序一致性检查通过；407 个 USER CODE 区域完整保留。
