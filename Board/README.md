# 固定板级工程

`OMNICTL2_3.ioc` 为当前 STM32H723 / 2613 的 CubeMX 输入。CubeMX 管理 HAL 初始化、引脚、时钟、DMA/NVIC 与 `Core/`、`Drivers/`、`cmake/` 等生成内容；SDK 自维护 `Support/`、`Linker/`、本目录的 CMake 接入与构建 presets。日常从 SDK 根目录构建。

## CubeMX CLI 生成

日常使用可执行的 Python 入口（Python 标准库，无额外依赖）：

```sh
python3 Board/generate.py
python3 Board/generate.py --dry-run
python3 Board/generate.py --cubemx /path/to/STM32CubeMX --timeout 300
```

也可以从其他工作目录通过脚本的绝对路径调用。`--cubemx` 优先于环境变量 `STM32CUBEMX`；默认使用本机 macOS 安装位置，否则查找 PATH 中的 `STM32CubeMX`。脚本根据自身位置定位当前 checkout，在 `build/cubemx/<时间戳-唯一后缀>/` 保存 `.mx`、`generate.log`、`timing-check.log`、`changed-files.txt` 和生成前 `Board-before` 快照（排除 build/.idea/__pycache__）。生成失败时保留现场，不自动回滚。

内部通过 `subprocess.run` 调用 CubeMX `-q`，不经 shell 拼接命令。临时 `.mx` 使用带引号的 IOC **绝对路径**，再执行 `project generate` 和 `exit`。IOC 使用绝对路径，不依赖进程工作目录；路径错误会使生成失败，即使 `-q` 也可能弹出错误窗口。

退出码和生成 `OK` 均符合预期后，脚本运行 I2C 时序一致性检查；任一步失败返回非零。`--dry-run` 不创建文件或启动 CubeMX。脚本不自动编译、提交、烧录或操作调试会话。生成后仍需人工复核差异并执行相应构建。

`generate.mx` 中可能包含特定 checkout 的路径；日常使用 `generate.py` 自动定位。CubeMX 版本以实际执行日志为准。

生成前保留整个 Board 和当前未提交改动快照；生成后比较实际内容，确认 `USER CODE`、SDK 自维护文件、HAL/CMSIS、CMake/工具链和中断所有权。IOC 启用 `KeepUserCode=true`。生成不自动包含编译或烧录。

## I2C4 当前配置

400 kHz Fast mode，固定 PLL3R=60 MHz，模拟滤波开启、数字滤波 0。IOC、生成的 `hi2c4.Init.Timing` 与 Support 常量均为 `0x10C7182D`。重新生成后核对三处一致性，并复核全部生成差异。

启动仍经 USER CODE 调用 Support，验证时钟/滤波并应用相同固定时序；详细合同见 [Support](Support/README.md)。时序一致性检查为 `python3 Tools/Debug/check_i2c_timing.py`（从 SDK 根目录执行），现同时检查 IOC、生成代码和 Support。

生成、构建与板测结果见 [CHANGELOG](CHANGELOG.md)；原始证据入口见[验证方法](../Docs/Validation.md)。

## Support 组织

System/IO/Devices/USB 四组分别维护运行支撑、总线接入、固定设备接线和 USB 底层适配。源码统一在 Support/component.cmake 显式登记，包含路径为私有。指示器提示策略不在 Board。职责与 IRQ 所有权见 [Support](Support/README.md)。

ADC3五通道监测使用 `Support/Devices/monitor_adc_board.c`，由SystemMonitor独占采样；IOC、生成ADC初始化、BDMA_Channel2 IRQ与链接脚本保持原配置。D3 使用专用32字节对齐缓冲区，完成/错误回调由Support持有。

## CAN 启动配置与 80 MHz 基线

FDCAN1–3 的 IOC/生成基线使用 PLL2Q 80 MHz、时钟分频 1，默认仍为 Classic 1 Mbit/s（nominal prescaler=1、TSEG1=60、TSEG2=19、SJW=13）。自维护 `Support/IO/can_native_board.c` 在 HAL handle 尚未启动时验证实际时钟、应用冻结的 `SystemConfig.can[]`，设置格式、位时序及 Message RAM 后重新 HAL Init。运行期不切换 Classic/FD，恢复沿用已冻结配置。应用 FD 配置须显式提供仲裁、数据速率与 BRS，合法组合见 [CAN](../Source/IO/CAN/README.md#启动模式与-fd-接口)。

三路 Message RAM 起点固定为 0、`0x300`、`0x600` word，每路预算 `0x300` word，共用硬件 RAM 为 2560 word；Classic/FD 任意组合均检查窗口边界，不根据相邻通道模式挪动地址。

| 每路模式 | RX / TX 布局 | 实占 word |
|---|---|---|
| Classic | FIFO0/FIFO1 各 8、专用 RX 3；专用 TX 4、TX FIFO 28、TX event 8；payload 8 B | 232 |
| FD | FIFO0 8、TX FIFO 28、TX event 8；FIFO1/专用 RX/专用 TX 关闭；payload 64 B | 676 |

两种布局均保留标准/扩展 filter 各 4 项。FD 高于 1 Mbit/s 的 BRS 数据阶段启用板级 TDC 配置；实际位速率、物理采样裕量须用仪器与目标线束确认。CAN4 独立使用 MCP 的 40 MHz 时钟和现有 SPI 10 MHz；Classic 32 RX＋32 TX/8 B，FD 16 RX＋12 TX/64 B（2016 B RAM），不占 STM32 FDCAN Message RAM。

FD 使用下文 P0/P1 IRQ 策略和既有任务优先级；DMA bank、共享 SPI 的 IMU 优先仲裁和生成文件所有权按对应模块合同维护。实际负载边界须按帧长、速率、接线和并发业务验证。

## ITCM 容量分配

自维护链接脚本将 CAN Router 的 `.itcm_bss` 与 `.debug_watch` 放入 ITCM，DMA 仍在 D2、BDMA 仍在 D3、Fault retained 仍在 D2。`main` 的现有早期入口以 64 位写入初始化 ITCM BSS/Watch，并复制代码；不修改生成启动汇编、IOC 或 HAL。全局构造不得提前访问专用段。容量/验证见 [ITCM 迁移](../Docs/Validation/ITCM-20260929/README.md)。

## IRQ 分级

NVIC 的 FDCAN1–3、IMU EXTI9_5、SPI4 及其 DMA 为 5；MCP EXTI15_10、五路 UART IRQ 及十路 UART DMA IRQ 为 6。修改 IOC 或生成入口后检查这些值与任务优先级合同一致。

```sh
# SDK 根目录，只检查配置并预览生成命令。
python3 Board/generate.py --dry-run
python3 Tools/Debug/check_i2c_timing.py
```

主机构建与板端运行分别验收，生成历史和实际测量保存在 [CHANGELOG](CHANGELOG.md)。
