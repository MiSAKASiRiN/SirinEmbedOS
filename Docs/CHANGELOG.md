# 文档历史与维护记录

## 历史记录归档：Development.md

以下原文是整理前的阶段记录，文件名、板上状态与验证结论仅适用于记录当时。

### 迁移收尾与后续开发

截至 2026-10-06，49 个 Framework 的迁入、拆分或排除已登记，主机与固件构建已有通过记录。当前阶段是源码集成完成后的运行验收，尚不能视为可发布的板端基线。来源见 `Migration/README.md`，既有验证证据见 `Validation.md`。

首次上板已由用户完成：曾进入 RUNNING、采到新鲜 IMU，但启动屏障对 `0x3` 返回值的误判造成复位循环。源码已修正，直接连接及软件复位复测未再现旧断言。现已推进 2613 传感器驱动、持续采样与 CDC1 双 IMU 遥测；2026-09-27 四颗传感器均 READY，双 IMU 短测约 1 kHz。后续已有 CAN1/CAN3、ADC 与 I2C/EEPROM 器件短测；持久化与静态校准为更新的主机/构建成果。当前待验收项以下表及 [验证索引](Validation.md) 为准，不把首次上板记录当成当前固件状态。

## 父项目任务开发约定

父项目新任务遵循 [根目录接入说明](../README.md#托管任务与父项目内核接口) 和 [Kernel 使用指南](../Source/Kernel/README.md)：

1. 将长期业务拆成 init（一次）、step（一轮）、cleanup（清理），按周期/事件/单次选择模式；循环、普通等待与统计由 SDK 提供。
2. 需要可管理句柄时，在 ApplicationMain/应用管理任务中先准备参数与同步对象，再 ThreadCreate 并保存返回句柄；固定启动任务可声明在配置列表，配置回调内不创建线程。
3. 通过 GetInfo 区分对象创建、初始化结果与退出状态；事件生产者使用通知或队列，先建立生命周期/就绪同步再开始发送。
4. 管理者负责协作停止和 Delete；超时或 BUSY 后保留参数与对象，停止生产者后再完成资源回收。不要把任务函数返回、Join 成功当成内存已回收。
5. 父项目常规代码只使用公开 Kernel/IO/Services API，不直接创建/删除原生 RTOS 线程、不插统计 hook。内核专项验证中的原生适配留在 Tests 或明确的测试适配文件。

先用 [Minimal](../Examples/Minimal/README.md) 验证接入和创建，再在真实业务负载下检查周期、CPU、资源余量与 I/O；SDK 固有服务使用 SRNOS_ 前缀，应用不能使用该前缀或删除系统线程。已有 [板端短测](Validation/ManagedTasksBoard-20260928/README.md) 不替代父项目自己的完整负载验收。

## 当前底层开发与优化优先级

2026-09-28 全面命名/公共隔离后的固件已获授权重新烧录。托管任务、统计开关、四颗传感器连续采样、ADC 软件故障注入、CDC 背压/DTR 重连的当前结果见 [SRNOS 板测](Validation/SRNOSBoard-20260928/README.md)。新增 [CAN1/CAN3 与 ADC 单点记录](Validation/CAN13-ADC-20260928/README.md)。以下区分功能缺口、优化和仍缺实测的合同；首次启动已不再是尚未执行的工作。

| 优先级 | 工作 | 完成依据 |
|---|---|---|
| P0 | 单份持久化及高频遥测板端验收 | 已实现 FT24C128A 单份 CRC32 记录、稳定ID参数、身份与逐IMU校准和1kHz最新值遥测；需要真实掉电、参数升级/重启、保存期间INS/I2C/CAN共存和8路遥测实测。单份不承诺掉电恢复旧值；已实现手动静止陀螺零偏校准，多姿态加速度标定仍待开发。 |
| P0 | CAN1–4 与共享 SPI4 的真实负载验收 | 2026-10-06当前优化/FD固件CAN1/3短接：Classic8B/FD8M64B各3k收发60秒CPU32.00%/35.13%、零接收丢帧，FD5M64B同档10秒通过；4M及以下64B过载。双IMU/INS保持1kHz，见 [实板记录](Validation/CANFD-Board-20261006/README.md)。旧固件6000/8000帧每秒CPU76.77%/95.21%仅为历史参考，未做相同固件/流量A/B。追加CAN1/4共享SPI板测见 [报告](Validation/CAN4-SPI-20261006/README.md)：Classic各3k生产周期不足、FD64B各3k硬件RX溢出；8M需修正跨控制器仲裁采样点一致性。优先优化CAN4事务/唤醒/锁及RX服务延迟；CAN2、四路同步、UART期限及物理故障恢复仍待验收。 |
| P1 | 统计成本与后台唤醒余量（优化） | 本轮开关对照接近新增 1 个百分点目标；需覆盖真实父应用和 CAN/CDC 组合负载。hook 与 Monitor 当前短测通过不证明所有负载。CAN adapter 仍保留 1 ms tick/poll_tx，CAN4 RX 有 5 ms 兜底；若进一步降频，先补 deadline/通知合同。 |
| P1 | UART/RS485 与真实总线故障（验收） | 接入已知外设验证 DMA 跨圈、TC、DE/RE、静默间隔、总超时；SPI/I2C 做断线、卡总线、恢复失败及迟到 DMA。主机故障模型和 ADC 软件注入不能替代物理故障。 |
| P1 | 生产可靠性（功能与验收） | 当前 IOC/SDK 未接入 IWDG/WWDG 监督。若要求无人值守恢复，需要明确健康上报、喂狗条件、故障保留及复位原因合同；同时做长时间 FPU/libc/同步并发、实际 USB 拔插及掉电复测。当前生命周期/TLS 短测已通过。 |
| P1 | 内存/栈证据与长期压力（验收） | 最新 Minimal Release D1 链接余量 73248 B，ITCM 余量 2432 B（[CAN 优化布局](Validation/CANRouter-20261002/README.md)），固定父对象池 32 KiB；不等于业务可额外分配同样字节。默认周期扫栈关闭，应在专用压力实验做有控制的一次性水位检查，再决定是否调整栈/队列，避免盲目缩减。 |
| P1 | 温控收敛和温稳保持（本轮实测线索） | 正常固件一分钟内 LSM 59.895–60.723°C，INS 温稳正常 8.696 秒后再次降级，姿态持续有效。先延长热平衡记录，再复核 PI、温度噪声及进入/退出判据；不通过直接放宽阈值宣称达标。 |
| P1 | ADC 与惯性测量质量（验收/标定） | ADC 已对照仪表约 24 V/0.091 A：均值 24.641 V/0.08986 A，电压偏高 2.67%，保持原倍率。需同步仪表并核对实际分压、ADC 引脚与 VDDA，再做多点标定。IMU/INS 连续性不等于方向、温漂、动态精度或绝对航向验收；用户标定需与持久化一起实现。 |
| P2 | HostLink、父应用与发布基线 | 注册/去重/锁外回调主机回归、CDC 板测已有证据；长命令超过 2 秒、持续 RX、CDC0 日志并发及满活动表仍需组合板测。保留真实父应用接入、来源许可和固件/配置/证据清单。既有重构已提交为 820b777，CAN 优化尚未提交；未发布。 |

未接入的 FatFs 属于新增能力；静态 IPv4/UDP/Ping 已于2026-10-14通过固定 lwIP 2.2.1接入，见 `Source/Services/Network` 及父项目三层验证记录；MSC/DFU 也不在本轮迁移范围。机器人任务、swerve、比赛功率策略、具体裁判 UI 布局留在父项目，不作为 SDK 迁移遗漏。

## 2026-10-06 板测新线索

- 六个CAN板测镜像均报告身份加载 `SRNOS_STORAGE_CORRUPT (6)`，参数加载成功、INS未出现UNCALIBRATED。下一步只读核对身份记录的版本/长度/CRC；不能据此称全部EEPROM损坏，本次未写入或格式化。
- LIS磁力计个别2–60秒窗口平均68.2–99.6Hz，空载和恢复档也出现；双IMU/INS各1kHz，四传感器错误/overrun无增长。应检查LIS采样连续性与共享I2C调度，尚无证据归因为CAN高负载。温稳超时/降级依旧存在，不以INS VALID代替温稳验收。

证据与原始数据见 [CAN1/3实板记录](Validation/CANFD-Board-20261006/README.md)，当前板已恢复测试前原固件。

## 2026-10-03 初检遗留问题

以下在本次 OS 初检中确认，CAN 优化未修改这些模块；不属于本轮通过回归即可排除的问题。

| 优先级 | 问题与触发 | 当前证据 / 后续处理 |
|---|---|---|
| P1 | 实测功率超预算但模型低估时可能不降额 | [PowerControl.cpp](../Source/Algorithms/Power/PowerControl.cpp) 用实测值决定是否进入限制分支，非关键电机缩放却仍按估算功率计算。主机复现：预算 50 W、有效实测 100 W、估算 20 W，连续五轮 scale=1、命令 10 保持 10。应补测量与模型不一致的约束策略，不能将当前算法当作实测硬功率上限。 |
| P1 | 长微秒忙等待存在无法退出风险 | [Time.c](../Source/Diagnostics/Time/Time.c) 将 cycle 需求饱和到 UINT32_MAX，循环只在差值恰好等于最大值时退出；550 MHz 下 DelayUs(8000000) 已触发饱和，循环可能反复错过唯一一周期窗口。改为有界分段或使用更宽时间差，并补饱和/回绕测试；目前为源码推导，未在实板调用该长等待。 |
| P2 | Fault 可读 Flash 范围超出本 MCU | [fault_platform.c](../Board/Support/System/fault_platform.c) 接受 0x08000000..0x09FFFFFF，但当前 H723 头与 linker 为 1 MiB、末址 0x080FFFFF。损坏的栈指针落入空洞可能通过检查后被解引用，导致异常处理中再次 fault；应按实际银行/完整区间校验，补错误指针测试。此为静态风险，未主动触发硬件二次故障。 |
| P2 | 参数 DebugPort dump 未接通且吞掉写失败 | [Parameters.cpp](../Source/Services/Parameters/Parameters.cpp) 的 DumpDebugPort 回调忽略 WriteExact 返回值；当前运行源码只有 RegisterWriteSink 定义、没有注册调用，DebugPort 返回 NOT_CONFIGURED，但 dump 返回 OK。应接入现有 logging sink 或明确向调用者返回失败；正常 USB 日志路径不因此被判定失效。 |
| P2 | Release 命名门禁误报标准库/TLS | 当前工具不识别编译器实例化的 std 模板与 errno TLS wrapper，基线及本轮均报 13 项；Debug 命名检查通过。需精准分类工具链生成符号，不应豁免整个 SDK 文件。 |

这些问题的范围与 CAN 优化的板测缺口分开处理。FatFs/LwIP、硬件看门狗与长时间组合负载仍按上表推进。

## 已有 BringUp 入口与验收用途

现有 `Examples/BringUp`，使用 SDK 公开接口和任务观测能力，在原应用线程中采集启动、堆、任务栈、IMU 和日志状态。没有添加系统线程、改变默认绑定或修改生成文件；独立构建目录使 Minimal 保持默认入口。

建议运行验收按以下顺序推进，每一步单独记下失败现象和实测证据：

1. BringUp 首次启动：调试器心跳、时基、堆和栈，再检查 USB CDC0 输出。
2. 固定服务：IMU 数据持续更新，SPI4 并发与 CAN4，再分别验证 CAN1–3；核对温控和 EEPROM 行为。
3. UART：按 PrototypeBindings 逐个接入 DR16、VT03、Referee、日志，再验证 RS485/H12H。
4. HostLink/Parameters：使用 Parameters 示例完成参数授权、持久化和断连恢复。
5. 在真实父项目运行负载下复测余量、时延、FPU/TLS、Fault 和错误恢复。

BringUp 是观测工具，不能自动完成上述验收；本节是原 BringUp 开发记录；后续已授权烧录 SensorBringUp，最新硬件证据以 Validation 文末为准。

## 2613 当前优先事项

- LSM 已读到 0x6A 并持续采样，早期无 MISO 响应的根因没有仅凭软件确定；下一步用 CDC1 CLI 完成双 IMU 十分钟窗口和实际 USB 拔插/拥塞实验。
- LIS/SPA/BMI/LSM 与 MCP 的局部持续采样已有证据；仍需真实 CAN 帧负载、方向、精度和实际声光验证。
- FT24C128A 已有独立阻塞/非阻塞器件驱动，并通过 16 KiB 整片及边界读写、备份恢复板测（`Validation/EEPROM-20260928/README.md`）。已接入参数/INS 单份持久化，默认启用启动加载和 HostLink 存储命令；旧 24C02 入口及格式删除。新记录真实掉电验证仍待完成。共享 I2C4 已有周期保留槽及后台分发，EEPROM 32 B 分段，现为 400 kHz Fast mode；正常整片压力下 LIS/SPA 连续性短测通过，整片写含校验约 5.12 秒、读约 1.03 秒（`Validation/I2C400-20260928/README.md`）。模拟边沿、断线/卡总线/掉电与长期负载仍待测。ADC/PHY 不在本轮范围。
- 已实现正式 INS：固定板内旋转、启动安装旋转、Mahony双六轴输入、恒温与降级、机器人系快照。Sensors/原始遥测保留器件轴；已接入单份校准持久化及上位机触发静止陀螺零偏校准，仍需验证实板精度。正式接口见 INS.md。

2026-09-28 正式 INS 短测与三个10秒窗口见 Validation.md 最新条目。低动态 RMS 明确按三轴分量整体口径；静置融合通过不代表动态精度、绝对航向或无人机/机器人闭环已验收。

## Board/Support 维护

板级支撑按 System/IO/Devices/USB 归类，新增文件须登记 Support/component.cmake。服务策略不放入 Board；固定硬件宏不从公共头开放。维护索引见 [Support](../Board/Support/README.md)，Source 中保留 HAL/寄存器依赖的理由见 [边界审查](BoardSupport.md)。

## 历史记录归档：BoardSupport.md

以下原文是整理前的阶段记录，文件名、板上状态与验证结论仅适用于记录当时。

### Board/Support 职责审查

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



## 2026-10-17 专题使用文档整理

- HostLink、INS、Sensors、Storage、SDKMigration 保留当前协议/布局和使用边界；补充配置、读取和托管任务示例。
- 修正 INS/CAN4 的现行线程优先级与单 worker 描述、主机串口占位符和 INS 示例工具链调用；历史验证摘要不作为当前运行保证。

### HostLink.md：原阶段记录

以下内容保留原表述，仅用于历史追溯；其中旧配置和旧时间点不可当作当前使用方式。

端口必须显式选择 **CDC1**，路径随主机枚举变化；上面的路径是 2026-09-27 本机观测值。

### HostLink.md：原阶段记录

以下内容保留原表述，仅用于历史追溯；其中旧配置和旧时间点不可当作当前使用方式。

相对之前source指针/getter表和20组低频调度，HostComm对象缩减417 B，合计静态对象净增3426 B；对象之间链接间隙和最终D1用量以构建map为准。

### INS.md：原阶段记录

以下内容保留原表述，仅用于历史追溯；其中旧配置和旧时间点不可当作当前使用方式。

旧 `lib_imu/lib_ins` 全局状态、方向预设、旧融合 worker、校准/持久化及两套温控已删除；没有旧服务选择接口。独立的 quaternion EKF 数学组件和外置 OMX IMU CAN 协议适配保留，它们不参与本服务。

### INS.md：原阶段记录

以下内容保留原表述，仅用于历史追溯；其中旧配置和旧时间点不可当作当前使用方式。

独立 INS/温控 worker 为 High5、3 KiB、1 ms，只读取传感器快照，不访问总线。BMI在DMA完成通知后由原采样线程当周期发布，消除额外1ms发布延迟；保留 BMI/LSM 采样及环境线程，顺序为 SPI High6 > 采样与 INS High5 > MCP High4。运行循环不分配内存、不打印、不等待 USB；使用 DWT 实际 dt，每次只推进一次，不追赶补算。

### Sensors.md：原阶段记录

以下内容保留原表述，仅用于历史追溯；其中旧配置和旧时间点不可当作当前使用方式。

驱动提供识别、配置回读、持续采样、状态与统计。融合和安装坐标变换由独立 [INS 服务](INS.md) 提供；磁航向、高度和用户标定尚未实现。板端证据及未通过项见 [Validation.md](Validation.md)。2026-09-27 短测已识别四颗器件，双 IMU 持续采样；较早的 LSM 离线记录保留在历史验证条目中，不代表当前结果。精度及长时间稳定性仍未验收。

### Sensors.md：原阶段记录

以下内容保留原表述，仅用于历史追溯；其中旧配置和旧时间点不可当作当前使用方式。

SPI dispatcher Realtime1 / 49 / 3 KiB；BMI P0 / 48 / 2 KiB、LSM P0 / 48 / 2 KiB；MCP RX/TX 各 P1 / 47 / 4 KiB。LIS/SPA 共用 Normal/2 KiB 线程，等待芯片转换时释放 I²C，不增加每器件线程。新增线程与八个完成信号已计入保守启动预算；请求及采样 buffer 静态分配，采样循环不分配内存。RTOS 线程/信号资源仍在启动时从 heap_5 创建。

### Storage.md：原阶段记录

以下内容保留原表述，仅用于历史追溯；其中旧配置和旧时间点不可当作当前使用方式。

截至 2026-09-29，以当前自维护实现为准。FT24C128A 器件读写已有板测；这里的单份记录、业务参数持久化及新静态校准已通过主机/构建检查，尚未完成对应真实断电和组合负载验收。历史结果不自动覆盖新固件。返回 [文档导航](README.md)。

### SDKMigration.md：原阶段记录

以下内容保留原表述，仅用于历史追溯；其中旧配置和旧时间点不可当作当前使用方式。

本轮是源码不兼容迁移。旧名称、旧头路径和空兼容入口已删除，不提供别名；父项目与 SDK 一起重新编译。命名与依赖隔离阶段保持既有 COBS/HostLink 编码及硬件格式；后续持久化阶段引入新的 FT24C128A 单份记录，明确不识别旧 24C02 格式，并新增消息号。不能把“命名阶段不改协议”理解为所有历史 EEPROM 都可兼容。已有 USB 描述字符串和调试记录 magic 不因源码命名改变。

### SDKMigration.md：原阶段记录

以下内容保留原表述，仅用于历史追溯；其中旧配置和旧时间点不可当作当前使用方式。

CAN RX/TX、HostComm/HostCmd、Indicators/SystemMonitor 保持分离；本轮不压缩默认线程栈和队列。HostComm 每轮 RX 上限 1024 字节，CAN RX worker 上限 32 帧；原生 FDCAN ISR 仍排空硬件 FIFO，需要板端负载验证。积压会继续安排处理，输出和同优先级任务仍有运行机会。SPI、Serial、Indicators 按事件与最近截止点等待，CAN 的外部适配器 tick/poll 合同保留。

### SDKMigration.md：原阶段记录

以下内容保留原表述，仅用于历史追溯；其中旧配置和旧时间点不可当作当前使用方式。

新增资源是固定 D1 状态及同步控制块：Serial 通道数组每槽预留两个静态二值信号量控制块，共 12 份（含 NONE 槽，最多初始化 10 个）；HostLink 活动身份表及一份待发送结果；CAN4 增加一个失败重试期限。没有新增常驻线程或运行期动态分配。静态增加量、完整 ELF 的 D1/D2/D3、heap、TLS 与默认预算见 [本轮验证](Validation/SRNOSBoundary-20260928/README.md)。`SRNOS_SystemHeapBudget()` 是 RTOS 启动堆预算，不能把静态 D1 增量当成新增 heap 分配。

### SDKMigration.md：原阶段记录

以下内容保留原表述，仅用于历史追溯；其中旧配置和旧时间点不可当作当前使用方式。

主机行为测试、独立公共头检查、父项目集成、Debug/Release 构建证明源码和静态合同。此次没有烧录；同负载 CPU/唤醒次数/临界区、INS/CAN/传感器与 CDC 拥塞仍须板测。循环 hook ≤1 µs、统计增量 ≤1 个百分点、Monitor 平均 CPU ≤0.1%、监测临界区 ≤10 µs 均不得用主机结果替代。

### SDKMigration.md：原阶段记录

以下内容保留原表述，仅用于历史追溯；其中旧配置和旧时间点不可当作当前使用方式。

2026-10-17：`SRNOS_NetworkConfig` 追加 `udp_rx_select[4]`，默认 NULL 兼容原单槽
满时丢新包语义。应用可在启动时选择 latest 或 latch；锁存事件不因超时/断线消失，
直到单消费者取走。`SRNOS_NetworkStatus` 尾部追加覆盖/锁存计数。整体重编译使用方；
不得把可能过期的速度命令标为 LATCH，不得在回调中直接操作控制器。
