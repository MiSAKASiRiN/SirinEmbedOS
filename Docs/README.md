# SirinEmbedOS 文档导航

第一次接入，先用 [QuickStart](../QuickStart.md) 编译模板父工程。随后在 [初始化配置](Config.md) 选择 CAN 电机、UART 设备和协议；在 [独立文件任务示例](../Examples/StartupTask/README.md) 添加自己的周期任务。

## 遥控与通信

| 任务 | 指南与示例 |
|---|---|
| **接 VT03/VT13，读取摇杆、档位、键鼠** | [VT03 接入与快照](../Source/Services/RemoteControl/README.md#vt03vt13-接入) · [UART9 只接收示例](../Examples/PrototypeBindings/README.md) |
| 在 VT03 上双向收发自定义数据或回传遥测 | [VideoLink 配置与线格式](../Source/Services/VideoLink/README.md) · [InputMessages 完整示例](../Examples/InputMessages/README.md) |
| 接 DR16、WFLY W.Bus 或 VT02/VT12 | [各来源的通道与按键](../Source/Services/RemoteControl/README.md) · [W.Bus 校准](../Source/Services/RemoteControl/WBus/README.md) · [VT02/VT12 专题](VT02VT12.md) · [VT02/VT12 示例](../Examples/VT02VT12/README.md) |
| 接入裁判系统、普通 UART、RS485 和 H12H | [裁判系统](../Source/Services/Referee/README.md) · [Serial](../Source/IO/Serial/README.md) · [RS485 / Modbus](../Source/IO/RS485/README.md) · [H12H](../Source/Devices/H12H/README.md) |
| 注册输入来源和业务消息 | [Input 与 SirinRMT](../Source/Services/Input/README.md) · [Message 路由](../Source/Services/Message/README.md) |
| 连接 USB 主机，注册命令与遥测流 | [双 CDC](../Source/Services/USB/README.md) · [SirinLink 服务 API](../Source/Services/SirinLink/README.md) · [线协议](SirinLink.md) |
| 使用 Ethernet、UDP、Ping 和 NTP | [LAN8720](../Source/Devices/LAN8720/README.md) · [Ethernet](../Source/IO/Ethernet/README.md) · [Network](../Source/Services/Network/README.md) · [Clock](../Source/Services/Clock/README.md) |

## 总线、设备与算法

| 任务 | 指南与示例 |
|---|---|
| 接电机、注册 CAN 回调或使用 CAN FD | [CAN 路由](../Source/IO/CAN/README.md) · [电机型号与 C620](../Source/Devices/Motors/README.md) · [CAN FD 示例](../Examples/CANFD/README.md) · [外置 MCP2518FD](../Source/Devices/MCP2518FD/README.md) |
| 接入其他外部设备 | [超级电容](../Source/Devices/Supercapacitor/README.md) · [导航与视觉 CAN](../Source/Devices/NaviVision/README.md) · [TL42R](../Source/Devices/TL42R/README.md) |
| 访问 SPI/I²C 或读取板载传感器 | [SPI](../Source/IO/SPI/README.md) · [I²C](../Source/IO/I2C/README.md) · [传感器与时序](Sensors.md) · [采样 API](../Source/Devices/Sensors/README.md) |
| 读取姿态、温控和校准状态 | [INS 接入](INS.md) · [IMU 算法](../Source/Devices/IMU/README.md) · [INS 服务](../Source/Services/INS/README.md) |
| 计算 PID、滤波或功率约束 | [控制算法](../Source/Algorithms/Control/README.md) · [数学与滤波](../Source/Algorithms/Math/README.md) · [功率约束](../Source/Algorithms/Power/README.md) |
| 使用指示灯和提示音 | [Indicators](../Source/Services/Indicators/README.md) · [WS2812](../Source/Devices/WS2812/README.md) · [蜂鸣器](../Source/Devices/Buzzer/README.md) |

## 任务、数据与诊断

| 任务 | 指南与示例 |
|---|---|
| 创建或管理线程，查看栈及对象池 | [Kernel](../Source/Kernel/README.md) · [启动列表注册](../Examples/StartupTask/README.md) · [运行期创建与回收](../Examples/Minimal/README.md) |
| 查看日志，发布只读变量 | [Logging](../Source/Services/Logging/README.md) · [Telemetry](../Source/Services/Telemetry/README.md) · [发布示例](../Examples/Telemetry/README.md) |
| 声明可写参数、保存到 EEPROM | [Parameters](../Source/Services/Parameters/README.md) · [父工程示例](../Examples/Parameters/README.md) · [主机 CONFIG 协议](../Source/Services/Parameters/SirinLink/README.md) |
| 读取身份、校准或黑匣子记录 | [EEPROM 数据布局](Storage.md) · [Storage 服务](../Source/Services/Storage/README.md) · [FT24C128A](../Source/Devices/EEPROM/README.md) · [NOR](../Source/Devices/NOR/README.md) · [Blackbox](../Source/Services/Blackbox/README.md) |
| 检查 CPU、任务周期、时间或异常记录 | [SystemMonitor](../Source/Services/SystemMonitor/README.md) · [运行统计](../Source/Diagnostics/Runtime/README.md) · [计时](../Source/Diagnostics/Time/README.md) · [Debug Watch](../Source/Diagnostics/Watch/README.md) · [Fault](../Source/Core/README.md) |

## 工具与维护

- 主机连接、日志、命令、参数和遥测采集使用 [srneos CLI](../Tools/CmdLineTool/README.md)。[独立采集脚本](../Tools/SirinLink/README.md) 还支持逐样本 CSV。工具验证范围见 [CLI 验收说明](../Tools/CmdLineTool/ACCEPTANCE.md)。
- 维护固件时查阅 [系统服务索引](../Source/Services/README.md)、[开发与验证](Development.md)、[CI/CD](../CICD/README.md)。板级配置和再生成见 [Board](../Board/README.md)、[板级绑定](BoardSupport.md)、[I²C 时序检查](../Tools/Debug/README.md)。
- 第三方来源和许可随 [FreeRTOS](../ThirdParty/FreeRTOS/README.md)、[lwIP](../ThirdParty/LwIP/README.md)、[USB Device](../ThirdParty/USBDevice/README.md)、[CMSIS-DSP 快照](../ThirdParty/CMSISDSP/PROVENANCE.json)、[板级 HAL/CMSIS](../Board/Drivers/) 及其他供应商源文件维护。
- 版本变化和当时的验证范围见 [根 CHANGELOG](../CHANGELOG.md) 及模块变更记录。公开源码包含接入示例，本地测试套件、BringUp 固件与原始实机资料单独保留。
