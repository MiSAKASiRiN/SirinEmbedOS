# 固定板级支撑

本目录维护 STM32H723 / 2613 与 SDK 的接入，取代原来的 Glue 平铺目录。CubeMX 拥有 IOC、HAL 初始化和生成入口；Support 拥有板级接线与运行支撑，服务策略属于 Source。这里是 SDK 私有实现，不是应用 include 入口。

## 职责索引

| 目录 | 文件与用途 |
|---|---|
| System | `board_startup.h` 声明生成入口；`memory_early.c` 早期内存；`kernel_board.c` heap_5 区域与启动；`fault_platform.c/.h` 和 `fault_handlers.c` 捕获及异常入口；`libc_memory.c`、`libc_arena.h`、`libc_locks.c` 支撑 Picolibc |
| IO | `spi_board.c/.h` SPI4 HAL/DMA、模式和 abort；`i2c_board.c/.h` I2C4 handle、空闲检查与时序；`i2c_timing.h` 固定 TIMING；`can_board.cpp/.h` 三路 FDCAN 和 MCP 单一绑定；`serial_board.c` UART DMA、RS485 电气与回调 |
| Devices | `sensors_board.c/.h` 总线/片选/地址；`sensor_frames.c/.h` 固定安装坐标；`heater_board.c/.h` 加热 PWM；`indicators_board.c/.h` 指示器初始化和 IRQ；三个 `*_hardware.h` 固定蜂鸣器/WS2812 硬件常量 |
| USB | `usb_board.c/.h` 检查 PCD 并准备 PA11/12；`usb_device_board.c` USBD_LL、FIFO、PCD 回调与端点接入 |

`component.cmake` 集中登记源码，保留 board/components 两个现有编译归属。不要扫描目录自动编入，也不要在其他模块清单重复列出这些文件。私有 include 使用 `System/…`、`IO/…`、`Devices/…`、`USB/…`。

## 启动与调用关系

1. `main` 的 USER CODE 在 MPU/cache 前调用 `SRNOS_BoardEarlyInitialize`，完成专用段初始化和 Fault 准备。
2. CubeMX 完成 HAL 初始化；I2C USER CODE 调用 `SRNOS_BoardI2CApplyTiming` 校验时钟/滤波并应用固定值。
3. `SRNOS_BoardStartKernel` 接入 Kernel；冻结配置后由 Indicators 服务准备声光，服务调用板级初始化。heap_5 注册入口仍是 `osKernelInitialize`。
4. Kernel 创建原有服务线程；Indicators worker 保持 Low/1024 B，按事件与最近音符/心跳截止点等待，按配置执行启动提示和 500 ms 心跳。Support 不拥有提示策略或 worker。
5. USB 服务在类注册前调用 `SRNOS_BoardUSBPrepare`，随后装配 CDC/日志并启动协议栈。准备失败不进入协议栈，后续启动失败仍清理并允许重试。

## 中断和回调所有权

| 入口 | 唯一有效实现 |
|---|---|
| NMI、HardFault、MemManage、BusFault、UsageFault、DebugMon | System/fault_handlers.c；生成同名函数由 Board CMake 改名 |
| DMA1 Stream0/1 | Devices/indicators_board.c 转给蜂鸣器/WS2812 私有 DMA handle；生成同名函数改名 |
| SVC、PendSV、SysTick | FreeRTOS/CMSIS；HAL tick 仍由生成的 TIM7 通路提供 |
| GPIO EXTI HAL callback | IO/can_board.cpp 转给外置 CAN |
| UART callbacks | IO/serial_board.c 按现有注册方式接入 |
| SPI、I2C、FDCAN callbacks | Source/IO 中各自唯一通路处理；不为目录整齐搬走事务状态机 |
| PCD callbacks / MSP | USB/usb_device_board.c 转给协议栈；强 MSP 初始化仍由生成的 usb_otg.c 持有，适配中的 weak fallback 不接管它 |

其余向量保持生成文件所有权。不得新增同名入口或在驱动中重复接管同一资源。

## 新增代码放哪里

- 固定 handle、GPIO、片选、IRQ 接线、安装坐标、板级补丁：本目录对应组。
- 队列、并发、事务和错误恢复：Source/IO；器件协议、寄存器和采样：Source/Devices。
- 提示策略、周期 worker、USB 生命周期、温控/融合：Source/Services 对应模块。
- 公开函数与类型：Include/SirinEmbedOS；不得包含 Support 私有头。固定硬件宏从公共头移除，不保留旧路径或应用覆盖入口。
- 每个新增实现登记显式构建清单；修改行为、接口或所有权时同步 README/CHANGELOG 和对应验证。

完整边界审查见 [BoardSupport](../../Docs/BoardSupport.md)，历史文件对应见 [迁移映射](../../Docs/Migration/board-support-relocations.json)。

## I2C4 Fast mode

`IO/i2c_timing.h` 定义 `0x10C7182D`，与 `Board/OMNICTL2_3.ioc` 同步。固定 PLL3R 为 60 MHz、模拟滤波开启、DNF=0；PRESC/SCLDEL/SDADEL/SCLH/SCLL = 1/12/7/24/45。按 SCL/SDA 上升及下降时间各不超过 300 ns 计算，最快约 394.7 kHz，属于 400 kHz Fast mode，实际周期受边沿/滤波/clock stretching 影响。

`MX_I2C4_Init` 在初始化及滤波配置后经 USER CODE 调用 `SRNOS_BoardI2CApplyTiming`。入口仅在启动时、总线空闲且配置匹配时关闭 PE、写 TIMINGR 并重新开启 PE，不支持运行时切速。IOC 显式选择 `I2C_Fast`。2026-09-28 已通过 CubeMX 6.18.1 CLI `project generate`，生成区现为同一个 `0x10C7182D`；入口继续验证时钟和滤波合同，并应用相同固定值。后续重新生成仍需核对三处一致性，见 [生成记录](../../Docs/Validation/CubeMX-20260928/README.md)。

本版 HAL 的 `HAL_RCCEx_GetPeriphCLKFreq` 没有 I2C4 分支，因此使用 I2C4 source 检查、PLL3 ready 检查和 `HAL_RCCEx_GetPLL3ClockFreq` 的 R 输出验证 60 MHz，不修改第三方 HAL。PB7/PB8 开漏、外部上拉、DMA/NVIC/MPU 和其他时钟配置保持原状。

`python3 Tools/Debug/check_i2c_timing.py` 检查寄存器字段、IOC 一致性和 Fast-mode 时序约束。它不证明实际边沿满足 300 ns，需要示波器确认。板端数据见 [400 kHz 记录](../../Docs/Validation/I2C400-20260928/README.md)。

## ADC3 监测接入

Devices/monitor_adc_board独占hadc3、BDMA_Channel2和五个halfword的32字节对齐D3缓冲区。完成/错误HAL回调唯一归此文件，BDMA IRQ仍由生成代码调用HAL_DMA_IRQHandler；不改IOC/生成内容。首次校准由SystemMonitor任务执行，HT关闭，TC/错误仅记录并通知。停止须同时确认ADC关闭与BDMA EN清零，否则上层隔离，禁止复用缓冲区。仅临时屏蔽BDMA_Channel2 IRQ以关闭HAL默认HT，不全局屏蔽中断等待硬件。

## 2026-09-28 板端验证

ADC3 停止在 HAL 停止请求源后显式关闭 BDMA Channel2，并核验硬件使能位；适配 HAL normal 完成 READY 但 EN 保留的行为，不修改 HAL 或 CubeMX 生成文件。

2026-09-29：System 早期入口继续在 MPU/cache 前调用 Core 内存初始化；Core 现负责 ITCM CAN Router BSS/Watch 的 64 位清零和代码复制。Support 不接管服务初始化；生成文件、DMA/BDMA 绑定不变。见 [ITCM 迁移记录](../../Docs/Validation/ITCM-20260929/README.md)。
