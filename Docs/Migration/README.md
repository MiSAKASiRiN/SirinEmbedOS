# Framework 迁移台账

来源：`/Users/sirin/CLionProjects/h723vg-v2-prototype` 的实际工作区快照。原 Framework 共 **49** 个目录；没有迁移任何嵌套 `.git`、`.gitmodules` 或 gitlink。SDK 自身的根 `.git` 保留。

- `source-manifest.json` 是导入前原始记录：父仓库commit/status、每个模块的URL/commit/status，以及 **323** 个原文件SHA-256。包含排除模块和未导入文档的hash；不等于323个文件全部迁入。
- `migration-ledger.json` 是实施结果：逐模块处理、许可情况、原文件hash和最终目标文件hash。拆分/重写/新增文件以说明及 `SDK-SHA256SUMS` 为准。旧链接片段合并进Board维护的完整脚本。
- `header-map.json` 记录最初头文件路径迁移，最终编译不依赖这个表或prototype路径。
- 未发现独立LICENSE的自维护模块没有被擅自补写开源许可证；已有源码内版权/许可声明原样保留，逐项状态见JSON。Microchip驱动保留其原许可声明。
- `lib_swerve` 不属于独立Framework子仓库，其commit/status是prototype父仓库信息；文件hash才是该目录的精确快照。
- 外围USB/glue来源另见 `ThirdParty/USBDevice/ADAPTATIONS.json` 与本目录 `glue-references.json`；上游FreeRTOS、USBDevice与CMSISDSP在各自 `ThirdParty` 目录保留版本/来源、许可和校验记录。没有升级版本。

| 原目录 | 归属 | 结果 | 来源commit | 导入时本地修改 |
|---|---|---|---|---|
| `SirinOS` | `Core` | 拆分：Core公共操作和Fault；板级异常/输出在Board/Glue；OS符号整体改SRNOS。 | `2ebb2025f122` | 无 |
| `bsp_bmi088` | `Devices/IMU/BMI088` | 迁入：按能力归属整合，保留原协议与worker行为；公共头集中到Include/SirinEmbedOS。 | `56977aeb879d` | 无 |
| `bsp_buzzer` | `Devices/Buzzer` | 迁入：按能力归属整合，保留原协议与worker行为；公共头集中到Include/SirinEmbedOS。 | `eb9d907b1348` | 无 |
| `bsp_dr16` | `Services/RemoteControl/DR16` | 拆分：保留协议/流解帧，Serial持有DMA与HAL；RemoteControl统一worker。 | `516a8b0e0b20` | 无 |
| `bsp_dwt` | `Diagnostics/Time` | 迁入：按能力归属整合，保留原协议与worker行为；公共头集中到Include/SirinEmbedOS。 | `d90e8047a409` | 无 |
| `bsp_eth_lan8720a` | `—` | 排除：仅构建占位，本批不迁移网络栈。 | `ddae15578257` | 无 |
| `bsp_fdcan` | `IO/CAN/Native` | 迁入：固定三路FDCAN由Board绑定，IO持有路由和收发。 | `e5bbc9c8eb41` | 无 |
| `bsp_i2c` | `IO/I2C` | 迁入并修正：固定I2C4、D3 BDMA；按设备完成状态，确认abort及迟到IRQ隔离。 | `4be281de29a4` | 无 |
| `bsp_spi` | `IO/SPI` | 迁入并重写：SPI4唯一High6分发、8槽复制队列、IMU/普通公平仲裁、确认取消及故障隔离。 | `9e14264ce8ff` | 无 |
| `bsp_srn_log` | `Services/Logging` | 迁入：普通日志使用USB/SWO/已声明Serial通道，删除强制复位USART1路径。 | `3f25c3722f38` | 无 |
| `bsp_vt03` | `Services/RemoteControl/VT03` | 拆分：保留协议/流解帧，Serial持有DMA与HAL；RemoteControl统一worker。 | `2237b8cfea59` | 无 |
| `bsp_ws2812` | `Devices/WS2812` | 迁入：按能力归属整合，保留原协议与worker行为；公共头集中到Include/SirinEmbedOS。 | `8e350616fb6f` | 无 |
| `lib_24c02` | `Devices/EEPROM` | 迁入：按能力归属整合，保留原协议与worker行为；公共头集中到Include/SirinEmbedOS。 | `8429b956d71c` | 无 |
| `lib_adp_dji_c610` | `Devices/Motors/C610` | 迁入：按能力归属整合，保留原协议与worker行为；公共头集中到Include/SirinEmbedOS。 | `da50148ea143` | 无 |
| `lib_adp_dji_c620` | `Devices/Motors/C620` | 迁入：按能力归属整合，保留原协议与worker行为；公共头集中到Include/SirinEmbedOS。 | `6422fe74992b` | 无 |
| `lib_adp_dji_gm6020` | `Devices/Motors/GM6020` | 迁入：按能力归属整合，保留原协议与worker行为；公共头集中到Include/SirinEmbedOS。 | `d06581764dfd` | 无 |
| `lib_adp_dm_4310` | `Devices/Motors/DM4310` | 迁入：按能力归属整合，保留原协议与worker行为；公共头集中到Include/SirinEmbedOS。 | `97817a4e77f7` | 无 |
| `lib_adp_dm_4340` | `Devices/Motors/DM4340` | 迁入：按能力归属整合，保留原协议与worker行为；公共头集中到Include/SirinEmbedOS。 | `3e7d58ee749f` | 无 |
| `lib_adp_lk_mg8016e` | `Devices/Motors/LK8016E` | 迁入：按能力归属整合，保留原协议与worker行为；公共头集中到Include/SirinEmbedOS。 | `60f83e518700` | 无 |
| `lib_adp_navision` | `Devices/NaviVision` | 迁入：按能力归属整合，保留原协议与worker行为；公共头集中到Include/SirinEmbedOS。 | `3f4613c16407` | 无 |
| `lib_adp_supcap` | `Devices/Supercapacitor` | 迁入：按能力归属整合，保留原协议与worker行为；公共头集中到Include/SirinEmbedOS。 | `31712a7e071b` | 无 |
| `lib_algos` | `Algorithms/Math` | 迁入：按能力归属整合，保留原协议与worker行为；公共头集中到Include/SirinEmbedOS。 | `3bf012afb323` | 无 |
| `lib_controls` | `Algorithms/Control` | 迁入：按能力归属整合，保留原协议与worker行为；公共头集中到Include/SirinEmbedOS。 | `9918558a4dec` | 无 |
| `lib_debug_watch` | `Diagnostics/Watch` | 迁入：按能力归属整合，保留原协议与worker行为；公共头集中到Include/SirinEmbedOS。 | `14a265c0ed37` | 无 |
| `lib_generic_ui` | `Services/Referee/UI` | 拆分：只保留ui_interface协议编码；ui_Main布局留给应用。 | `2f3282d8bfa2` | 无 |
| `lib_imu` | `Devices/IMU` | 拆分并替换：保留BMI驱动、Mahony及独立EKF数学/OMX协议；删除旧全局INS、方向预设、校准存储及温控worker。正式INS与统一温控位于Services/INS，原始采样位于Devices/Sensors。见Docs/INS.md。 | `67372a0cad61` | 无 |
| `lib_mcp2518fd` | `Devices/MCP2518FD` | 迁入：保留Microchip源码内许可；器件寄存器与CAN路由分开，Board管理引脚。 | `08f0442c4b9a` | 无 |
| `lib_modbus_rtu` | `IO/RS485/Modbus` | 迁入并适配共享RS485完整请求/应答调度。 | `2bbf0a29d47f` | 无 |
| `lib_mpu_dcache` | `Core/Memory` | 拆分：公共内存合同在Core/Memory；早期初始化及完整链接布局在Board。 | `96da546834b4` | 无 |
| `lib_params` | `Services/Parameters` | 迁入并优化：显式业务可写授权、边界检查、冻结注册、一致快照与原EEPROM格式。 | `65ff1da3f455` | 无 |
| `lib_power_control` | `Algorithms/Power` | 拆分：只保留通用功率估计/限额模型，移除裁判/SuperCap比赛策略依赖。 | `41bba925810e` | 无 |
| `lib_referee` | `Services/Referee` | 拆分：协议/状态与Serial传输分离，绑定初始化后冻结。 | `ff6e61d483fe` | 无 |
| `lib_remote_control` | `Services/RemoteControl` | 迁入：来源仲裁保留，优先级/超时/映射注册冻结。 | `acec94f35a31` | 无 |
| `lib_rs485_lc_tl42r` | `Devices/TL42R` | 迁入：使用RS485总线endpoint，配置时声明，默认不控制设备。 | `e46539b4559a` | 无 |
| `lib_serial_servo_h12h` | `Devices/H12H` | 迁入并适配Serial事务；默认未绑定，不假设板卡具备单线电气接线。 | `b4d9d696ada7` | 无 |
| `lib_serial_servo_hd` | `—` | 排除：没有可迁入的实际实现。 | `d98f0e8e8e56` | 无 |
| `lib_srn_cobs` | `Services/HostLink/Codec` | 迁入：按能力归属整合，保留原协议与worker行为；公共头集中到Include/SirinEmbedOS。 | `c076719104b6` | 无 |
| `lib_swerve` | `—` | 排除：底盘业务与机器人配置留在父项目；该目录由prototype父仓库管理。 | `4ae5428c6fb9` | 父仓库有修改，逐文件hash为准 |
| `lib_uart_router` | `—` | 替换：物理端口/Serial通道/协议/服务拆分；旧路由接口不保留。 | `e97fc233f686` | 无 |
| `tskptt_exfdcan_router` | `IO/CAN/External` | 迁入：按能力归属整合，保留原协议与worker行为；公共头集中到Include/SirinEmbedOS。 | `42f24a98c2cf` | 无 |
| `tskptt_host_comm` | `Services/HostLink` | 迁入并完善：注册冻结，CDC1会话清理、FIFO遥测和有界处理；SensorBringUp禁用存储。新增双IMU遥测与CLI见Docs/HostLink.md。 | `9c426abdad48` | 无 |
| `tskptt_hskp` | `Diagnostics/Runtime` | 迁入：按能力归属整合，保留原协议与worker行为；公共头集中到Include/SirinEmbedOS。 | `6af1f6b3bb23` | 无 |
| `tskptt_imu` | `Devices/IMU/Worker` | 迁入：按能力归属整合，保留原协议与worker行为；公共头集中到Include/SirinEmbedOS。 | `5f074ed34e39` | 无 |
| `tskptt_log` | `Services/Logging/Worker` | 迁入：按能力归属整合，保留原协议与worker行为；公共头集中到Include/SirinEmbedOS。 | `e2bc6e68dc8c` | 无 |
| `tskptt_ntfdcan_router` | `IO/CAN/Router` | 迁入：按能力归属整合，保留原协议与worker行为；公共头集中到Include/SirinEmbedOS。 | `44d1eb5ad784` | 无 |
| `tskptt_param_console` | `Services/Parameters/HostLink` | 迁入：HostLink CONFIG协议保留，只能操作明确可写业务参数；增加启动冻结的LOAD/SAVE权限门控。 | `27c1789e28ae` | 无 |
| `tskptt_referee` | `Services/Referee/Receiver` | 迁入：按能力归属整合，保留原协议与worker行为；公共头集中到Include/SirinEmbedOS。 | `7bb74524cb52` | 无 |
| `tskptt_referee_tx` | `Services/Referee/Transmitter` | 合并：发送经Serial队列；兼容的任务函数仅退出，不默认创建冗余TX线程。 | `4951ab2763e7` | 无 |
| `tskptt_rs485_router` | `IO/RS485` | 重写：固定UART7总线，多个endpoint串行事务、TC完成/应答超时/静默间隔。 | `c8a66ad6af4f` | 无 |

## 配置和行为边界

`SRNOS_ApplicationConfigure` 填写启动配置、注册CAN适配器和业务参数。Serial建立通道并由内置服务领取后，再执行可选 `bind_channels` 声明Raw/H12H/RS485设备；随后注册表与架构配置冻结。参数数值仅允许显式授权项运行期变更。绑定、线程描述、IMU编译配置不进入参数可写表。

普通UART默认不占用。PrototypeBindings示例为 USART6=DR16、UART8=Referee、UART9=VT03、USART1=日志；UART7是固定RS485电气端口，按应用实际设备注册。H12H默认不绑定。

固定CAN、IMU、SPI/I2C、EEPROM和板载指示能力集成在SDK；电机使能、目标、机器人任务、机构控制、安全策略与裁判UI布局归父项目。

## 新增和修正的接线

- `Board/Glue/fixed_buses.*`、`fixed_resources.*` 统一SPI4/I2C4/CAN实例和MCP引脚；Serial单独持有UART HAL callback。
- `Board/Glue/indicators.c` 接管DMA1 Stream0/1至各自私有handle；生成IRQ函数只通过构建宏改名，生成原件未搬动。
- `Source/Kernel/kernel.c` 完成配置冻结、唯一调度器启动、系统线程装配，以及参数恢复后的应用线程屏障。设备失败不会把FreeRTOS任务返回到端口fatal trap。
- SPI/I2C回调High6、IMU High5、MCP High4、片内CAN High相对顺序保留。Serial使用一个共享worker；RemoteControl协议/仲裁共用worker；Referee TX使用Serial队列。
- 默认Fault只保留RAM记录；配置阶段可明确预留紧急输出。正常Boot/日志不触发USART1重置。
- `.data/.bss/TLS`位于D1；I2C4 BDMA staging位于D3；DMA/Watch/retained位于D2。链接验证会检查可达性和边界。

验证命令、结果与板端待测项见 `../Validation.md`。编译/主机测试不代表板端外设已验证。
