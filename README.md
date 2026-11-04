# SirinEmbedOS

Input/SirinRMT 同时支持原始通道、按键、相对增量与有类型的车体系平面速度；速度使用 SI 单位并独立过期，运动类型切换通过 motion_epoch 通知应用撤权，机器人限速/使能由父项目决定。详见 [Input 合同](Source/Services/Input/README.md)。

面向固定 STM32H723 / OmniCtrl 主控的应用开发 SDK。CubeMX 管理硬件配置与 HAL；SDK 管理 FreeRTOS、总线、设备驱动及系统服务；父项目管理控制算法、设备组合和业务任务。

第一次接入请从 **[QuickStart](QuickStart.md)** 开始：构建示例、创建父工程、运行周期任务，再选择通信能力。[完整文档导航](Docs/README.md)用于查询接口；修改历史只记录在各级 [CHANGELOG](CHANGELOG.md)。

主机连接、烧录、Flash/地址读取、USB 日志/遥测、业务命令和主机测试统一入口见 [srneos CLI](Tools/CmdLineTool/README.md)。工具实现属于 SDK，父项目仅安装并提供配置；主机测试不等于实板验证。

## 能力与入口

| 能力 | 使用说明 |
|---|---|
| 托管任务、通知、同步与资源统计 | [Kernel](Source/Kernel/README.md) |
| 四路 CAN、Classic adapter 与原始 FD | [CAN](Source/IO/CAN/README.md)、[电机](Source/Devices/Motors/README.md) |
| 串口、遥控、裁判及 RS485 | [Serial](Source/IO/Serial/README.md)、[RemoteControl](Source/Services/RemoteControl/README.md)、[Referee](Source/Services/Referee/README.md)、[RS485](Source/IO/RS485/README.md) |
| 通用输入快照与业务消息 | [Input/SirinRMT](Source/Services/Input/README.md)、[Message](Source/Services/Message/README.md)、[完整示例](Examples/InputMessages/README.md) |
| VT02/VT12 双向 UART | [接入指南](Docs/VT02VT12.md)、[完整示例](Examples/VT02VT12/README.md)：115200/8N1 键鼠、自定义 RX 与显式 A5 TX |
| WFLY W.BUS 原始接收 | [WBus](Source/Services/RemoteControl/WBus/README.md)：独立解码及内置 wbus 输入来源，可按通道校准 |
| LAN8720、静态 IPv4、UDP 与 Ping | [Ethernet](Source/IO/Ethernet/README.md)、[Network](Source/Services/Network/README.md) |
| 启动时间与 NTP 绝对时间 | [Clock](Source/Services/Clock/README.md)：无授时保留 uptime，Unix 时间带有效性与 holdover |
| NOR 黑匣子、异步读回 | [Blackbox](Source/Services/Blackbox/README.md)：专用 SDK worker、固定 12 MiB 环区、有界队列 |
| 传感器、姿态与温控 | [Sensors](Docs/Sensors.md)、[INS](Docs/INS.md) |
| 日志、USB CDC 与上位机协议 | [Logging](Source/Services/Logging/README.md)、[USB](Source/Services/USB/README.md)、[SirinLink](Docs/SirinLink.md) |
| 业务参数、遥测与 EEPROM | [Parameters](Source/Services/Parameters/README.md)、[Telemetry](Source/Services/Telemetry/README.md)、[Storage](Docs/Storage.md) |
| 图传双向数据与自动遥测 | [VideoLink](Source/Services/VideoLink/README.md)：复用 VT03 UART，注册 `0310` 发送周期，接收 `0311` |
| ADC、CPU、周期与异常记录 | [SystemMonitor](Source/Services/SystemMonitor/README.md)、[Core](Source/Core/README.md) |

公共 C API 使用 `SRNOS_*`，C++ 位于 `SRNOS::` 下。应用只包含 `Include/SirinEmbedOS/` 公共头；HAL、CMSIS、FreeRTOS 和 SDK 私有头不向应用传播。已有源码升级参照 [SDKMigration](Docs/SDKMigration.md)。FatFs、TCP、DHCP、DNS、IPv6 不在当前接口范围。

自动构建、产物交付与父仓库流水线配置见 [CI/CD](CICD/README.md)。

## 构建与 CLion

使用 ST Arm Clang / Picolibc、CMake 和 Ninja。以下命令从 SDK 根目录执行，工具链路径按本机调整：

```sh
export PATH="/opt/ST/STM32CubeCLT_1.22.0/st-arm-clang/bin:$PATH"
cmake --preset Debug -DSIRINEMBEDOS_EXAMPLE=Minimal -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build --preset Debug -j 8
```

产物为 `build/Debug/SirinEmbedOS.elf` 和 `.map`，Release 使用 `Release` preset。CLion 打开 SDK 根目录；父工程开发时打开父工程根目录。构建不会烧录或启动板卡。

## 父项目接入

假设 SDK 子模块目录为 `SirinEmbedOS`、应用入口为 `App/app_main.cpp`：

```cmake
cmake_minimum_required(VERSION 3.22)
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/SirinEmbedOS/CMake/starm-clang.cmake")
project(MyRobot LANGUAGES C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_subdirectory(SirinEmbedOS)
sirinos_add_firmware(MyRobot App/app_main.cpp)
```

SDK 持有唯一 `main()` 和调度器入口。应用实现 `SRNOS_ApplicationConfigure()` 声明启动配置，实现 `SRNOS_ApplicationMain()` 创建业务任务；不要再次启动调度器。完整可编译源文件和配置头示例见 [QuickStart](QuickStart.md#创建父项目)。

## 托管任务与父项目内核接口

任务提供 `init/step/cleanup`，SDK 执行等待、循环和统计。需要管理句柄时，在 ApplicationMain 或应用任务中调用 ThreadCreate；固定启动任务可在配置回调填写 `threads[]`，该回调不能创建线程或阻塞 I/O。

- `step` 完成一轮工作，不再写永久循环或周期延时。应用名称为 1～15 字节，不使用 `SRNOS_` 前缀；优先级 1～39，栈按字节计。
- Create 成功与初始化成功分别检查返回值和 GetInfo。回调参数保留到 Delete 成功。
- 停止和回收使用 RequestStop / Join / Delete；Join 不释放对象，Delete 超时后保留句柄与参数。不能删除 SDK 服务或同步删除自身。
- 通信回调只做有界解析、复制或通知，业务状态由拥有它的任务修改。

生命周期、同步和 ISR 示例见 [Kernel](Source/Kernel/README.md)。堆预留、应用对象池占用与实际栈水位是不同指标，读取示例见 [QuickStart](QuickStart.md#检查任务和内存)。

## 配置与资源所有权

CAN/UART/IMU、网络、服务开关及注册在启动期冻结，恢复沿用冻结配置。PID 等业务数值使用显式参数注册，只读数据使用 Telemetry；不把硬件架构配置暴露为主机可写参数。

普通 UART 与 Ethernet 默认不绑定业务。Network 启用后独占原始 Ethernet 收发；UDP 选择器、最新值和事件锁存见 [Network](Source/Services/Network/README.md)。CAN adapter 时间与 TTL 使用 `SRNOS_TimeMs()`，不混用 HAL/DWT 原点。

固定设备能力默认包含，不表示上电自动使能外部电机。应用应核对传感器、INS、温控、存储加载和 USB 默认配置；禁用融合不等于取消传感器与 INS worker。

## 示例、维护与验证

- [Minimal](Examples/Minimal/README.md)：启动入口与周期任务。
- [Parameters](Examples/Parameters/README.md)、[Telemetry](Examples/Telemetry/README.md)：业务数值和只读变量。
- [InputMessages](Examples/InputMessages/README.md)：输入来源和应用消息。
- [Board](Board/README.md)、[开发流程](Docs/Development.md)：生成边界、源码装配和检查命令。

模块 README 和专题文档描述当前用法、示例及限制；修改原因、日期、验证结果和待验事项只写对应 CHANGELOG。构建成功不代表板端验收。

## 源码分发范围

包含 SDK 实现、固定板配置、API 用法示例和主机工具。测试套件、专用 BringUp 固件、现场日志、Flash/EEPROM/RAM 转储及一次性迁移脚本不随源码分发。构建入口见 [开发指南](Docs/Development.md)。

第三方版权和许可原样保留；自维护模块的来源与许可状态见 [迁移索引](Docs/Migration/README.md)，该索引不代表已经获得开源授权。
