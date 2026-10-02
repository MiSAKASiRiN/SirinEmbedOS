# 超级电容 CAN 适配

包含 `SirinEmbedOS/Devices/Supercapacitor/Supercapacitor.hpp`，使用 `SRNOS::Devices::SupCap`、状态和命令类型；帧为 `SRNOS::Devices::CANFrame`，ID 宏为 `SRNOS_SUPCAP_TX_STD_ID` / `SRNOS_SUPCAP_RX_STD_ID`。

父项目显式填写使能、功率和能量预算并接入 CAN router；一次性复位仍按原发送成功确认语义处理。适配对象与注册的生命周期一致；不把设备命令直接变为默认上电行为。

本轮名称和公共边界迁移保持协议/发送语义，公开 include 实现编译通过；具体外设收发和电源行为未在本轮板测。
