# CAN 电机适配

C++17 父项目包含 `SirinEmbedOS/Devices/Motors/<型号>/<型号>.hpp`，全部类、状态和装配函数位于 `SRNOS::Devices`，例如 `SRNOS::Devices::DJI_C610 motor{1};`。支持的型号为 C610、C620、GM6020、DM4310、DM4340、LK8016E。

反馈帧统一为 `SRNOS::Devices::CANFrame`，底层 POD 为 `SRNOS_CANDeviceFrame`；CAN router 使用自有 `SRNOS_CAN*` 公共接口。对象由父项目持有，注册后的生命周期须覆盖路由使用期。父项目明确选择设备、命令、发送周期和使能行为，包含本模块不会自行驱动电机。

本轮仅收敛名称、共享帧类型和 C++ 公共边界，协议字段、缩放与调度策略保持；已做只使用公开 include 的实现编译，固件链接由 SDK 接入检查覆盖，不代表电机实测。
