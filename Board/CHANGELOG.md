# Changelog

## Unreleased

- 2026-11-18：在 ST CMSIS 的 `ExitRun0Mode` 声明上将 Clang 排除在 GCC 的 `optimize("Os")` 属性分支之外。CubeMX 生成脚本对原声明补回同一判断。Minimal Debug 和 Release 完整构建中该警告均由 140 条降至 0 条，各自的可烧录镜像与修改前逐字节一致。C/C++ 的其他未知属性仍报警，ARM GCC 预处理保留原属性。已离线验证脚本处理 LF、CRLF、已修复及不同声明；未实际运行 CubeMX 或烧录。

- 2026-10-25：移除未装配的 GCC/Newlib 三项模板；生成脚本仅在 CubeMX 和 I2C 检查均通过后按精确名单归档移除，保留生成前快照及失败现场。ST Arm Clang/Picolibc、Board 独立构建和 HAL/CMSIS 生成边界保持。离线流程测试、dry-run 与 Board Release 构建通过；未实际运行 CubeMX 或烧录。

## 2026-10-06 CAN1/3 板测

- 验证 80 MHz 基线、Classic 1 Mbit/s 和 FD 1/2/4/5/8 Mbit/s 的合法 DLC、标准/扩展 ID 与收发 ACK。Classic 8 B、FD 8M/64 B 双向各 3k 持续 60 秒通过，FD 5M 同档 10 秒通过，4M 及以下同档过载；完整计数与 CPU/栈结果见 [CAN 验证记录](../Source/IO/CAN/CHANGELOG.md)。六个测试镜像逐段下载校验，结束时恢复原 1 MiB Flash 并完整读回，释放 GDB/J-Link。未修改生成配置或生产驱动；该次未覆盖 CAN2/4、物理信号裕量、Bus-Off 恢复及四路/UART 组合负载。

## 2026-10-03 FDCAN 基线与 P0/P1 分级

- FDCAN IOC/生成基线改用 PLL2Q 80 MHz，默认 Classic 1 Mbit/s；自维护 `can_native_board` 在 Start 前应用冻结的模式、速率/TDC 和 RAM 布局。三路窗口为 0/0x300/0x600 word，Classic/FD 各占 232/676 word，检查共享 2560 word 边界。两种布局均保留标准/扩展 filter 各 4 项；MCP 40 MHz/SPI 10 MHz 保持，Classic 32 RX＋32 TX/8 B，FD 16 RX＋12 TX/64 B 占 2016 B RAM，不占原生 Message RAM。该次 PCB 离线，新增时钟、FD/TDC 与物理通信尚待板测。
- 调整为 FDCAN1–3、IMU EXTI9_5、SPI4 及其 DMA 优先级 5；MCP EXTI15_10、五路 UART IRQ 和十路 UART DMA IRQ 为 6。实际生成与 I2C 检查通过，仅 `gpio.c/dma.c` 优先级变化；保持 DMA bank、任务优先级及共享 SPI 的 IMU 优先策略，未烧录。

## 2026-10-02 CAN 优先级生成

- FDCAN1–3 IT0/IT1、MCP EXTI15_10、SPI4 IRQ 从 6 调至 5；SPI4 DMA 原为 5，LSM EXTI9_5、UART IRQ/DMA 保持原值。生成与 I2C 检查通过，仅 `fdcan.c/gpio.c/spi.c` 变化，415 个 USER CODE 区域完整保留；Support/Linker/cmake、硬件时钟和 DMA 缓冲不变。未烧录或操作调试器，板端延迟待验；次日 P0/P1 调整另见上文。

## 2026-09-29 ITCM 布局

- 自维护 linker 增加 ITCM BSS，将 Watch 移入 ITCM，检查代码/BSS/Watch 合并容量及 64 位初始化对齐。D2 Fault retained 随段重排，仍排除启动清零；生成源码、IOC/HAL 未改。

## 2026-09-28 板级支撑与生成流程

- 支撑整理为 Support/System、IO、Devices、USB；指示器策略归服务，USB 准备和 MCP 默认配置收拢，蜂鸣器/WS2812 固定硬件宏私有化。保持时序、调度与内存合同；21 组主机、九个父项目目标及 Minimal/Board/EEPROMBringUp 构建通过，该项未生成、烧录或板测。
- SystemMonitor 替代 Diagnostics 线程，接入 1 Hz ADC/任务统计和 HostLink 发布，默认取消扫栈。ADC3 五通道由 SystemMonitor 独占，板级 BDMA 适配使用 D3 专用 32 B 对齐缓冲，Support 持有完成/错误回调；IOC、生成 ADC 初始化、BDMA_Channel2 IRQ、Indicators 与链接脚本不变。仅完成主机与构建检查。
- 新增 `generate.py` 的 CubeMX 选择、超时、dry-run、Board 快照与日志流程。首次相对 IOC 路径调用失败且 Board 未变；改为带引号绝对路径后，从 `/tmp` 调用生成及检查通过，内容无变化。当前调用方法见 [Board 指南](README.md)。
- CubeMX 6.18.1 实际生成返回退出码 0 和 `OK`，只更新 `Core/Src/i2c.c` 的 TIMING 为 `0x10C7182D`：PLL3R 60 MHz、400 kHz Fast mode、模拟滤波开启、数字滤波 0。407 个 USER CODE 区域及其余文件保留；Minimal Debug/Release、九个父项目构建和 IOC/生成代码/板级常量一致性检查通过，未烧录或接管板端调试会话。该记录不替代电气边沿和长期负载验收。
