# Changelog

## Unreleased

- 2026-10-06：实板CAN1/3验证80MHz基线、Classic1M及FD数据1/2/4/5/8M收发；所有合法DLC、STD/EXT通过，3k负载边界按帧长/速率记录。未修改生成配置或生产驱动；六个测试镜像逐段下载校验，最后完整恢复原1MiB Flash并读回一致，释放GDB/J-Link。CAN2/4、物理信号裕量和Bus-Off仍未验，见 [记录](../Docs/Validation/CANFD-Board-20261006/README.md)。

- 2026-10-03：FDCAN IOC/生成基线改用 PLL2Q 80 MHz，默认 Classic 1 Mbit/s；新增自维护 can_native_board 在 Start 前应用每路冻结的 Classic/FD 格式、速率/TDC 和 RAM 布局。固定窗口 0/0x300/0x600 word，Classic 各 232 word、FD 各 676 word，显式检查共享 2560 word 和通道窗口边界。P0/P1 IRQ、DMA bank、MCP 40 MHz/SPI 10 MHz 保持；PCB 离线，新增基线未烧录，位时序/物理通信待测。

- 2026-10-03：按 P0 CAN1/2/3＋IMU、P1 CAN4＋UART 更新 IOC 并实际 Generate：IMU EXTI9_5=5、MCP EXTI15_10=6、UART十路DMA=6；本轮仅 gpio.c/dma.c 生成差异，其他高优先级通路保持5。证据见 Priority-20261003，未烧录。

- 2026-10-03：按 CAN 最高通信优先级调整 IOC，并于 2026-10-02 用 generate.py 实际生成；FDCAN1–3/MCP EXTI/SPI4 IRQ 为 5。仅三个生成文件对应优先级变化，415 个 USER CODE 保留，I2C 时序一致；未改硬件时钟、DMA 缓冲或链接脚本，板端延迟待验。

- 2026-09-29：自维护 linker 增加 ITCM BSS，将 Watch 移入 ITCM，检查代码/BSS/Watch 合并容量与 64 位初始化对齐；D2 Fault retained 随段重排，但清零排除合同不变。生成源码、IOC/HAL 未改。Debug/Release、父项目及板端范围见 [记录](../Docs/Validation/ITCM-20260929/README.md)。

- 2026-09-28：SystemMonitor替代Diagnostics线程，1 Hz ADC/任务统计与HostLink发布；增加ADC板级BDMA接入、逐槽统计和主机工具，默认取消扫栈。迁移system_monitor_enabled及HostLinkPrepareEx合同；Indicators/生成文件不变。主机与构建验证见Docs/Validation.md，未烧录/板测。


- 2026-09-28：板级支撑整理为 Support/System、IO、Devices、USB；指示器策略归服务，USB 准备和 MCP 默认配置收拢，蜂鸣器/WS2812 固定硬件宏私有化。保留生成边界及现有时序/调度/内存合同；21 组主机、九个父项目目标、Minimal/Board/EEPROMBringUp 构建通过，未生成/烧录/板测。详见 Docs/Validation/BoardSupport-20260928/README.md。 · 2026-09-28

- 新增可执行 `generate.py`，支持指定 CubeMX、超时与 dry-run；系统调用前备份 Board，保存脚本/日志/变化清单，成功后检查 IOC/生成代码/Glue 时序。默认不构建、不烧录。
- 首次相对 IOC 路径触发 CubeMX 找不到文件及错误窗口，Board 未改动；改为动态带引号绝对路径后，从 `/tmp` 调用实测生成和时序检查通过，Board 内容无变化。AGENTS.md 与 README 已记录该约束。
- 使用 STM32CubeMX 6.18.1 `-q Board/generate.mx` 执行 `project generate`，退出码 0；生成的 I2C4 TIMING 更新为已板测的 `0x10C7182D`。
- 与生成前完整 Board 快照比较，仅 `Core/Src/i2c.c` 的时序赋值发生内容变化；USER CODE、自维护 CMake、HAL/CMSIS、IRQ 和 IOC 保持一致。生成日志及构建验证见 Docs/Validation/CubeMX-20260928，未烧录或操作板端调试会话。
- 生成后 Minimal Debug/Release、九个父项目构建及 IOC/生成代码/Glue 三处时序一致性检查通过；407 个 USER CODE 区域完整保留。

## 2026-10-17 文档整理

- 使用说明保留当前合同、入口和示例；日期战报与修改记录归入本文件。仅调整 Markdown，代码、原始测量数据和第三方源码未改动。

### README.md 中保留的历史说明

以下内容描述原文所述时期，不作为当前用法；当前入口见 [README.md](README.md)。

内部通过 `subprocess.run` 调用 CubeMX `-q`，不经 shell 拼接命令。临时 `.mx` 使用带引号的 IOC **绝对路径**，再执行 `project generate` 和 `exit`。macOS CLI 实测不能靠进程 `cwd` 解析相对 IOC 路径；相对路径会导致找不到 IOC，即使 `-q` 也可能弹出错误窗口。

现有 `generate.mx` 保留为手工调用记录，内部路径绑定本机 checkout；日常用 `generate.py` 自动定位，无需改该文件。使用应用实际日志确认版本，macOS bundle 元数据可能是旧值。本次 CLI 日志报告 6.18.1，退出码为 0，生成返回 `OK`。

400 kHz Fast mode，固定 PLL3R=60 MHz，模拟滤波开启、数字滤波 0。IOC、生成的 `hi2c4.Init.Timing` 与 Support 常量均为 `0x10C7182D`。2026-09-28 生成实际只改变了 `Core/Src/i2c.c` 的这一个赋值，保留所有 USER CODE 和其他文件内容。

生成和构建记录见 [CubeMX-20260928](../Docs/Validation/CubeMX-20260928/README.md)，此前硬件提速记录见 [I2C400-20260928](../Docs/Validation/I2C400-20260928/README.md)。生成后的固件本轮未烧录。

ADC3五通道监测使用新Support/Devices/monitor_adc_board，由SystemMonitor独占采样；IOC、生成ADC初始化、BDMA_Channel2 IRQ与链接脚本保持原配置。D3新增专用32字节对齐缓冲区，完成/错误回调由Support持有。

两种布局均保留标准/扩展 filter 各 4 项。FD 高于 1 Mbit/s 的 BRS 数据阶段启用板级 TDC 配置，本板CAN1/3已通过各速率短接收发，但物理采样裕量仍需仪器与实际线束确认。CAN4 独立使用 MCP 的 40 MHz 时钟和现有 SPI 10 MHz；Classic 32 RX＋32 TX/8 B，FD 16 RX＋12 TX/64 B（2016 B RAM），不占 STM32 FDCAN Message RAM。

FD 接入保留下文 P0/P1 IRQ 策略和既有任务优先级，不改变 DMA bank、共享 SPI 的 IMU 优先仲裁或生成文件所有权。2026-10-06已完成CAN1/3短接下80MHz与各FD速率的实际收发/ACK验证；Classic8B和FD8M64B各3k收发60秒通过，5M64B同档10秒通过，4M及以下64B同档过载。生成配置未因板测改变；CAN2/4、物理信号裕量、总线错误恢复及四路/UART组合负载未验。原Flash已完整恢复，见 [实板记录](../Docs/Validation/CANFD-Board-20261006/README.md)。

## 2026-10-02 CAN 优先级生成

IOC 将 FDCAN1–3 的 IT0/IT1、MCP EXTI15_10、SPI4 IRQ 从 6 调至 5；SPI4 DMA 原本为 5，LSM EXTI9_5、UART IRQ 与 DMA 保持原值。运行 `python3 Board/generate.py` 后 project generate OK、I2C 时序检查通过；生成差异仅 fdcan.c/gpio.c/spi.c 中上述优先级，415 个 USER CODE 区域完整保留，Support/Linker/cmake 不变。日志快照位于 `build/cubemx/20261002-234803-dyqnjq5f`，结果见 [记录](../Docs/Validation/CANRouter-20261002/README.md)。未烧录或操作调试器。

## 2026-10-03 P0/P1 调整

NVIC 的 FDCAN1–3、IMU EXTI9_5、SPI4 及其 DMA 为 5；MCP EXTI15_10、五路 UART IRQ 及十路 UART DMA IRQ 为 6。运行 `python3 Board/generate.py` 完成生成及 I2C 时序检查；本轮仅 gpio.c/dma.c 的优先级变化，快照为 `build/cubemx/20261003-002754-yuxutyfz`，验证见 [记录](../Docs/Validation/Priority-20261003/README.md)。前述 2026-10-02 记录保留当时策略。
