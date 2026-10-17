# Changelog

## Unreleased — Ethernet 二层接入

- 2026-10-13：新增 Ethernet 冻结配置、LAN8720A 链路管理、原始帧 API 与低优先级 worker；ETH IRQ 7、D2 DMA 新增 9216 B，现有时钟/MPU/生成区不变。默认关闭，父项目显式开启；详见 Source/IO/Ethernet 文档与父工程实测记录。

## Unreleased

- 2026-10-10：新增 fault_usart1.c/.h 并登记显式清单，致命异常默认接管 USART1 TX、HSI 和 PB14；应用自定义 Emergency writer 优先。正常 UART 所有权、生成区及链接段不变。五个寄存器模拟场景、固件编译与物理 USART1 Panic 输出通过，见 Docs/Validation/IMUTurntableLatency-20261010。

- 2026-10-06：自维护原生FD配置对齐MCP仲裁80%、数据≤4M80%/5M75%/8M80%；Classic、IOC和生成文件保持。 Classic目标3×60s＋600s通过，最高十秒CPU53.9632%，IMU保护线通过；FD格式/负载/恢复、资源及原Flash恢复见 [最终记录](../../Docs/Validation/CAN4-Optimization-20261006/README.md)。

- 2026-10-03：新增原生 CAN 冻结时序/RAM 适配，校验 80 MHz 与 CCU，按 Classic/FD 配置 TDC。固定窗口避免三路混合模式相互覆盖；主机寄存器替身、构建与内存检查通过范围见 [记录](../../Docs/Validation/CANFD-20261003/README.md)，PCB 离线未验证总线。

- 2026-09-29：同步说明早期初始化的 ITCM BSS/Watch 合同，保留现有 System 入口及生成边界；Core 完成 64 位初始化。构建、软件复位与实板 Watch 检查见 [记录](../../Docs/Validation/ITCM-20260929/README.md)。

- 2026-09-28：配合公共依赖隔离，SPI/I2C/CAN、WS2812/BMI/MCP 等板级装配引用私有头；自维护跨文件符号统一 SRNOS，CubeMX 与 HAL 规定入口保留，未改变固定硬件时序或内存段。验证边界见本轮总记录；没有 Generate 或烧录。

- 2026-09-28：修复 HAL normal BDMA 完成后 EN 保留导致 ADC 首轮误隔离；停止 ADC 后显式关闭独占通道并核验，新增真实 READY/EN 行为回归。正常采集及软件故障路径已板测，物理卡死未测。

- 2026-09-28：SystemMonitor替代Diagnostics线程，1 Hz ADC/任务统计与HostLink发布；增加ADC板级BDMA接入、逐槽统计和主机工具，默认取消扫栈。迁移system_monitor_enabled及HostLinkPrepareEx合同；Indicators/生成文件不变。主机与构建验证见Docs/Validation.md，未烧录/板测。


- 2026-09-28：板级支撑整理为 Support/System、IO、Devices、USB；指示器策略归服务，USB 准备和 MCP 默认配置收拢，蜂鸣器/WS2812 固定硬件宏私有化。保留生成边界及现有时序/调度/内存合同；21 组主机、九个父项目目标、Minimal/Board/EEPROMBringUp 构建通过，未生成/烧录/板测。详见 Docs/Validation/BoardSupport-20260928/README.md。 · 2026-09-28

- I2C4 增加固定 Fast-mode 时序与启动检查，IOC 同步为 `0x10C7182D`；仅在生成文件 USER CODE 添加调用，未执行 CubeMX regenerate。
- 时钟校验使用 PLL3R 专用查询，避开当前 HAL 通用频率查询不支持 I2C4 的情况；不改第三方 HAL、引脚、DMA、IRQ 或内存布局。
- Debug/Release 和父项目构建通过；硬件 TIMINGR 读回一致，数字 SCL 约 370 kHz，EEPROM/LIS/SPA 并发 21 项验证通过、零总线错误。模拟边沿尚未测量，详见 Docs/Validation/I2C400-20260928。
- 后续完成 CubeMX CLI 生成，生成的 TIMING 与 Glue 常量一致，USER CODE 入口完整保留；本轮未改变 Glue 运行行为，未重新烧录。

## 2026-10-17 文档整理

- 使用说明保留当前合同、入口和示例；日期战报与修改记录归入本文件。仅调整 Markdown，代码、原始测量数据和第三方源码未改动。

### README.md 中保留的历史说明

以下内容描述原文所述时期，不作为当前用法；当前入口见 [README.md](README.md)。

`MX_I2C4_Init` 在初始化及滤波配置后经 USER CODE 调用 `SRNOS_BoardI2CApplyTiming`。入口仅在启动时、总线空闲且配置匹配时关闭 PE、写 TIMINGR 并重新开启 PE，不支持运行时切速。IOC 显式选择 `I2C_Fast`。2026-09-28 已通过 CubeMX 6.18.1 CLI `project generate`，生成区现为同一个 `0x10C7182D`；入口继续验证时钟和滤波合同，并应用相同固定值。后续重新生成仍需核对三处一致性，见 [生成记录](../../Docs/Validation/CubeMX-20260928/README.md)。

## 2026-09-28 板端验证

2026-09-29：System 早期入口继续在 MPU/cache 前调用 Core 内存初始化；Core 现负责 ITCM CAN Router BSS/Watch 的 64 位清零和代码复制。Support 不接管服务初始化；生成文件、DMA/BDMA 绑定不变。见 [ITCM 迁移记录](../../Docs/Validation/ITCM-20260929/README.md)。

## 2026-10-06 优化后验证

Classic 1M/8B双向各3k完成3×60s＋600s，零丢帧/拒绝/TTL过期/硬件RX溢出，78个十秒CPU窗口最高53.9632%。双IMU/INS保持1kHz，含6k过载的完成间隔/采集/SPI排队峰值1143/151/112µs；FD五速率320格式用例和过载恢复通过，64B各3k在5/8M下CPU约61%。实测仅CAN1/CAN4短接，CAN2/3/UART同时满载与物理故障尚未覆盖；原Flash恢复并释放探针。详见 [最终验证与原始证据](../../Docs/Validation/CAN4-Optimization-20261006/README.md)。

`IO/ethernet_board.c/.h` 唯一拥有 ETH_IRQHandler 和 HAL ETH callbacks；IRQ 7，复用生成的 MSP/描述符，不调用 MX_ETH_Init。检查 MCO1 PLL1Q /11 的 50 MHz，再控制 PE14 复位 LAN8720A。D2 新增 9216 B DMA 缓冲，原段/MPU/时钟与生成代码不变。详细合同见 `Source/IO/Ethernet/README.md`。
