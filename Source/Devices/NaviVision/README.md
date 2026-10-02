# 导航与视觉 CAN 适配

包含 `SirinEmbedOS/Devices/NaviVision/NaviVision.hpp`，使用 `SRNOS::Devices::Navi`、`Vision` 及对应状态和 CAN 装配函数；帧为 `SRNOS::Devices::CANFrame`。默认 ID/超时编译宏使用 `SRNOS_NAVI_*`、`SRNOS_VISION_*`。

父项目持有适配对象并显式接入 CAN router，按快照判断在线、目标出现和数据有效性；本模块不创建业务线程，不触发运动或射击。移除旧全局 Navision 别名。

协议和数值保持，已完成公开头独立/实现编译的本模块检查；外部设备互通与控制策略需板端验证。
