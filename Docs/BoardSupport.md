# Board/Support 职责审查

2026-09-28 对当前 Source 中 HAL、CMSIS/寄存器和生成头文件依赖逐项检查。固定板卡允许 HAL 依赖；本轮不建立通用硬件 backend。下表中的保留项继续属于其模块，不能据此宣称已移除所有平台依赖。

## 本次边界调整

- Glue 拆为 Support 的 System/IO/Devices/USB。Fault、早期内存、heap/libc 仍是固定板级运行支撑。
- 指示器策略及 worker 进入 Services/Indicators；硬件时钟、初始化、IRQ 留在 Support。
- USB 服务原有 PCD 实例检查和 PA11/12 配置移入 Support/USB，顺序、失败与重试合同保持。
- MCP 驱动移除本地默认 SPI/CS 事实，与 CAN 初始化共用 Support 配置。配置类型不携带 router 私有状态。
- 蜂鸣器、WS2812/critical 的 GPIO/TIM/DMA/IRQ 宏从公共头移入三个私有硬件头，原值不变。设备的寄存器操作、开时钟顺序和波形实现保留。
- LED 数量、协议时序、队列容量、总线逻辑 ID、内存段属性是公开能力/资源合同，本轮不作为物理布线宏移除。

## Source 逐文件决定

| 文件 | 当前依赖证据 | 归属决定 |
|---|---|---|
| `Source/Devices/Buzzer/SRNOS_Buzzer.c` | `HAL_DMA_Abort`, `HAL_DMA_IRQHandler`, `HAL_DMA_Init`, `HAL_DMA_Start`, `HAL_GPIO_Init`, `HAL_GetTick`, `HAL_OK`, `HAL_RCC_GetClockConfig`, `HAL_RCC_GetPCLK1Freq`, `HAL_RCC_GetPCLK2Freq`, `HAL_StatusTypeDef`, `HAL_TIM_Base_Init`, `HAL_TIM_Base_Start`, `HAL_TIM_Base_Stop`, `__HAL_LINKDMA`, `__HAL_RCC_DMA1_CLK_ENABLE`, `__HAL_RCC_DMAMUX1_CLK_ENABLE`, `__HAL_RCC_DMAMUX_CLK_ENABLE`, `__HAL_TIM_DISABLE`, `__HAL_TIM_DISABLE_DMA`, `__HAL_TIM_ENABLE_DMA`, `__HAL_TIM_SET_AUTORELOAD`, `__HAL_TIM_SET_COUNTER`, `__HAL_TIM_SET_PRESCALER`, `__disable_irq`, `__get_PRIMASK`, `__set_PRIMASK` | 保留设备波形、GPIO/定时器/DMA 寄存器操作及开时钟序列；固定绑定宏已收回 Support 私有头。 |
| `Source/Devices/EEPROM/ft24c128a.c` | `HAL_GetTick`, `__disable_irq`, `__get_BASEPRI`, `__get_IPSR`, `__get_PRIMASK`, `__set_PRIMASK` | 保留器件采样、等待/超时、短临界区；总线与固定传感器接线由 IO/Support 管理。 |
| `Source/Devices/EEPROM/SRNOS_EEPROM24C02.c` | `__disable_irq`, `__enable_irq`, `__get_PRIMASK` | 保留器件采样、等待/超时、短临界区；总线与固定传感器接线由 IO/Support 管理。 |
| `Source/Devices/EEPROM/SRNOS_EEPROM24C02Bl.c` | `HAL_Delay`, `HAL_GetTick`, `__disable_irq`, `__enable_irq`, `__get_IPSR`, `__get_PRIMASK` | 保留器件采样、等待/超时、短临界区；总线与固定传感器接线由 IO/Support 管理。 |
| `Source/Devices/IMU/BMI088/SRNOS_BMI088.c` | `HAL_Delay`, `HAL_GetTick` | 保留器件采样、等待/超时、短临界区；总线与固定传感器接线由 IO/Support 管理。 |
| `Source/Devices/MCP2518FD/drv_spi.c` | `HAL_GetTick` | 保留传输和诊断时钟；移除 main.h，默认片选改用 Support 的唯一绑定表。 |
| `Source/Devices/Sensors/sensors.c` | `__disable_irq`, `__get_PRIMASK`, `__set_PRIMASK` | 保留器件采样、等待/超时、短临界区；总线与固定传感器接线由 IO/Support 管理。 |
| `Source/Devices/WS2812/SRNOS_WS2812.c` | `stm32h7xx_hal_dma.h`, `GPIOA`, `GPIOB`, `GPIOC`, `GPIOD`, `GPIOE`, `GPIOF`, `GPIOG`, `GPIOH`, `HAL_BUSY`, `HAL_DMA_Abort`, `HAL_DMA_IRQHandler`, `HAL_DMA_Init`, `HAL_DMA_RegisterCallback`, `HAL_DMA_Start_IT`, `HAL_DMA_XFER_CPLT_CB_ID`, `HAL_DMA_XFER_ERROR_CB_ID`, `HAL_GPIO_Init`, `HAL_NVIC_EnableIRQ`, `HAL_NVIC_SetPriority`, `HAL_OK`, `HAL_StatusTypeDef`, `__HAL_DMA_CLEAR_FLAG`, `__HAL_DMA_DISABLE_IT`, `__HAL_DMA_GET_COUNTER`, `__HAL_DMA_GET_FLAG`, `__HAL_RCC_DMA1_CLK_ENABLE`, `__HAL_RCC_DMAMUX1_CLK_ENABLE`, `__HAL_RCC_DMAMUX_CLK_ENABLE`, `__HAL_RCC_GPIOA_CLK_ENABLE`, `__HAL_RCC_GPIOB_CLK_ENABLE`, `__HAL_RCC_GPIOC_CLK_ENABLE`, `__HAL_RCC_GPIOD_CLK_ENABLE`, `__HAL_RCC_GPIOE_CLK_ENABLE`, `__HAL_RCC_GPIOF_CLK_ENABLE`, `__HAL_RCC_GPIOG_CLK_ENABLE`, `__HAL_RCC_GPIOH_CLK_ENABLE`, `__HAL_RCC_TIM1_CLK_ENABLE` | 保留设备波形、GPIO/定时器/DMA 寄存器操作及开时钟序列；固定绑定宏已收回 Support 私有头。 |
| `Source/Devices/WS2812/SRNOS_WS2812Critical.c` | `stm32h7xx_hal_dma.h`, `GPIOA`, `GPIOB`, `GPIOC`, `GPIOD`, `GPIOE`, `GPIOF`, `GPIOG`, `GPIOH`, `HAL_DMA_Init`, `HAL_DMA_Start`, `HAL_OK`, `RCC`, `__HAL_DMA_CLEAR_FLAG`, `__HAL_DMA_DISABLE_IT`, `__HAL_DMA_GET_COUNTER`, `__HAL_DMA_GET_DME_FLAG_INDEX`, `__HAL_DMA_GET_FE_FLAG_INDEX`, `__HAL_DMA_GET_HT_FLAG_INDEX`, `__HAL_DMA_GET_TC_FLAG_INDEX`, `__HAL_DMA_GET_TE_FLAG_INDEX`, `__HAL_RCC_DMA1_CLK_ENABLE`, `__HAL_RCC_DMAMUX1_CLK_ENABLE`, `__HAL_RCC_DMAMUX_CLK_ENABLE`, `__disable_irq`, `__enable_irq`, `__get_PRIMASK` | 保留设备波形、GPIO/定时器/DMA 寄存器操作及开时钟序列；固定绑定宏已收回 Support 私有头。 |
| `Source/Diagnostics/Time/SRNOS_Time.c` | `stm32h7xx_hal.h`, `CoreDebug`, `DWT`, `HAL_Delay`, `HAL_GetTick`, `__DMB`, `__disable_irq`, `__get_PRIMASK`, `__set_PRIMASK` | 保留 DWT/内核调试寄存器的计时观测实现；没有板外引脚选择。 |
| `Source/IO/CAN/External/SRNOS_ExternalCANDevice.cpp` | `stm32h7xx_hal.h`, `HAL_Delay`, `HAL_GetTick`, `__disable_irq`, `__enable_irq`, `__get_PRIMASK` | 保留 IO 事务、回调、期限及并发实现；具体 handle/引脚通过 Support 获取。 |
| `Source/IO/CAN/External/SRNOS_ExternalCANRxRouter.cpp` | `stm32h7xx_hal.h`, `HAL_GetTick`, `__disable_irq`, `__enable_irq`, `__get_IPSR`, `__get_PRIMASK` | 保留 IO 事务、回调、期限及并发实现；具体 handle/引脚通过 Support 获取。 |
| `Source/IO/CAN/External/SRNOS_ExternalCANTxRouter.cpp` | `stm32h7xx_hal.h`, `HAL_GetTick` | 保留 IO 事务、回调、期限及并发实现；具体 handle/引脚通过 Support 获取。 |
| `Source/IO/CAN/Native/SRNOS_FDCAN.c` | `DWT`, `HAL_ERROR`, `HAL_FDCAN_ActivateNotification`, `HAL_FDCAN_AddMessageToTxFifoQ`, `HAL_FDCAN_ConfigFilter`, `HAL_FDCAN_ConfigGlobalFilter`, `HAL_FDCAN_ErrorCallback`, `HAL_FDCAN_GetProtocolStatus`, `HAL_FDCAN_GetRxFifoFillLevel`, `HAL_FDCAN_GetRxMessage`, `HAL_FDCAN_GetTxEvent`, `HAL_FDCAN_GetTxFifoFreeLevel`, `HAL_FDCAN_RxFifo0Callback`, `HAL_FDCAN_RxFifo1Callback`, `HAL_FDCAN_STATE_BUSY`, `HAL_FDCAN_STATE_READY`, `HAL_FDCAN_Start`, `HAL_FDCAN_Stop`, `HAL_FDCAN_TxBufferCompleteCallback`, `HAL_FDCAN_TxEventFifoCallback`, `HAL_GetTick`, `HAL_OK`, `HAL_StatusTypeDef` | 保留 IO 事务、回调、期限及并发实现；具体 handle/引脚通过 Support 获取。 |
| `Source/IO/CAN/Router/SRNOS_CANRouterCore.cpp` | `stm32h7xx_hal.h`, `HAL_GetTick`, `__disable_irq`, `__get_PRIMASK`, `__set_PRIMASK` | 保留 IO 事务、回调、期限及并发实现；具体 handle/引脚通过 Support 获取。 |
| `Source/IO/CAN/Router/ntfdcan_rx_router.cpp` | `stm32h7xx_hal.h` | 保留 IO 事务、回调、期限及并发实现；具体 handle/引脚通过 Support 获取。 |
| `Source/IO/CAN/Router/ntfdcan_tx_router.cpp` | `stm32h7xx_hal.h`, `HAL_GetTick`, `__disable_irq`, `__enable_irq`, `__get_PRIMASK` | 保留 IO 事务、回调、期限及并发实现；具体 handle/引脚通过 Support 获取。 |
| `Source/IO/I2C/SRNOS_I2C.c` | `HAL_GetTick`, `HAL_I2C_AbortCpltCallback`, `HAL_I2C_ErrorCallback`, `HAL_I2C_GetError`, `HAL_I2C_GetState`, `HAL_I2C_IsDeviceReady`, `HAL_I2C_MasterRxCpltCallback`, `HAL_I2C_MasterTxCpltCallback`, `HAL_I2C_Master_Abort_IT`, `HAL_I2C_Master_Receive_DMA`, `HAL_I2C_Master_Transmit_DMA`, `HAL_I2C_MemRxCpltCallback`, `HAL_I2C_MemTxCpltCallback`, `HAL_I2C_Mem_Read_DMA`, `HAL_I2C_Mem_Write_DMA`, `HAL_OK`, `HAL_StatusTypeDef`, `__disable_irq`, `__get_BASEPRI`, `__get_IPSR`, `__get_PRIMASK`, `__set_PRIMASK` | 保留 IO 事务、回调、期限及并发实现；具体 handle/引脚通过 Support 获取。 |
| `Source/IO/SPI/SRNOS_SPI.c` | `HAL_GPIO_WritePin`, `HAL_GetTick`, `HAL_OK`, `HAL_SPI_ErrorCallback`, `HAL_SPI_GetError`, `HAL_SPI_GetState`, `HAL_SPI_RxCpltCallback`, `HAL_SPI_STATE_READY`, `HAL_SPI_TransmitReceive_DMA`, `HAL_SPI_TxCpltCallback`, `HAL_SPI_TxRxCpltCallback`, `HAL_StatusTypeDef`, `__disable_irq`, `__get_IPSR`, `__get_PRIMASK`, `__set_PRIMASK` | 保留 IO 事务、回调、期限及并发实现；具体 handle/引脚通过 Support 获取。 |
| `Source/Kernel/hooks.c` | `stm32h7xx.h`, `__disable_irq` | 保留调度器接入、异常 hooks 与中断屏蔽语义；heap/早期入口在 Support。 |
| `Source/Kernel/kernel.c` | `__disable_irq`, `__get_PRIMASK`, `__set_PRIMASK` | 保留调度器接入、异常 hooks 与中断屏蔽语义；heap/早期入口在 Support。 |
| `Source/Services/HostLink/host_link_service.c` | `stm32h7xx_hal.h`, `DWT`, `HAL_GetTick` | 保留服务期限、短临界区/屏障、诊断时间或 SWO 输出；这些是运行依赖，不是固定外设绑定。 |
| `Source/Services/HostLink/SRNOS_HostComm.c` | `stm32h7xx.h`, `__disable_irq`, `__get_PRIMASK`, `__set_PRIMASK` | 保留服务期限、短临界区/屏障、诊断时间或 SWO 输出；这些是运行依赖，不是固定外设绑定。 |
| `Source/Services/INS/ins_service.c` | `stm32h7xx.h` | 保留服务期限、短临界区/屏障、诊断时间或 SWO 输出；这些是运行依赖，不是固定外设绑定。 |
| `Source/Services/INS/ins_snapshot.c` | `stm32h7xx.h`, `__disable_irq`, `__get_PRIMASK`, `__set_PRIMASK` | 保留服务期限、短临界区/屏障、诊断时间或 SWO 输出；这些是运行依赖，不是固定外设绑定。 |
| `Source/Services/Indicators/indicators.c` | `DWT`, `HAL_GetTick`, `HAL_OK`, `HAL_StatusTypeDef` | 保留服务用时钟/有界等待；硬件时钟读取、初始化及 IRQ 已分到 Support。 |
| `Source/Services/Logging/SRNOS_SrnDebugPort.c` | `HAL_GetTick` | 保留服务期限、短临界区/屏障、诊断时间或 SWO 输出；这些是运行依赖，不是固定外设绑定。 |
| `Source/Services/Logging/SRNOS_SrnLog.c` | `CoreDebug`, `HAL_GetTick`, `__disable_irq`, `__get_PRIMASK`, `__set_PRIMASK` | 保留服务期限、短临界区/屏障、诊断时间或 SWO 输出；这些是运行依赖，不是固定外设绑定。 |
| `Source/Services/Parameters/SRNOS_Params.cpp` | `stm32h7xx_hal.h`, `HAL_GetTick` | 保留服务期限、短临界区/屏障、诊断时间或 SWO 输出；这些是运行依赖，不是固定外设绑定。 |
| `Source/Services/Parameters/ParameterLock.cpp` | `stm32h7xx.h`, `__DMB`, `__disable_irq`, `__get_PRIMASK`, `__set_PRIMASK` | 保留服务期限、短临界区/屏障、诊断时间或 SWO 输出；这些是运行依赖，不是固定外设绑定。 |
| `Source/Services/Referee/referee.c` | `__DMB` | 保留服务期限、短临界区/屏障、诊断时间或 SWO 输出；这些是运行依赖，不是固定外设绑定。 |
| `Source/Services/RemoteControl/SRNOS_RemoteControl.c` | `stm32h7xx.h`, `__disable_irq`, `__get_PRIMASK`, `__set_PRIMASK` | 保留服务期限、短临界区/屏障、诊断时间或 SWO 输出；这些是运行依赖，不是固定外设绑定。 |
| `Source/Services/USB/SRNOS_USBCdcChannels.c` | `stm32h7xx.h`, `__disable_irq`, `__get_PRIMASK`, `__set_PRIMASK` | 保留服务期限、短临界区/屏障、诊断时间或 SWO 输出；这些是运行依赖，不是固定外设绑定。 |
| `Source/Services/USB/usb_boot_replay.c` | `stm32h7xx.h`, `__disable_irq`, `__get_PRIMASK`, `__set_PRIMASK` | 保留服务期限、短临界区/屏障、诊断时间或 SWO 输出；这些是运行依赖，不是固定外设绑定。 |
| `Source/Services/USB/usbd_conf.h` | `stm32h7xx_hal.h`, `HAL_Delay` | 协议栈配置仍归服务；main.h 已换成 HAL 类型头，不再取得板卡引脚。 |

## 维护与兼容性

应用函数和数据类型保持；旧固定硬件宏及 Glue 私有路径不保留兼容。父项目应通过公开 SDK API 使用固定资源，不依赖 Support include 路径。新增绑定在 Support 显式登记，新增设备协议/服务策略分别放入 Devices/Services。

历史验证和导入快照保持原始路径，可通过 [重定位表](Migration/board-support-relocations.json) 查到当前文件；第三方 USB ADAPTATIONS 的历史 sdk_path 同样由此映射，不改写原始来源记录。

验证记录见 [BoardSupport-20260928](Validation/BoardSupport-20260928/README.md)。
