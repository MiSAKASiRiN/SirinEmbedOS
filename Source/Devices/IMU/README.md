# IMU 协议与算法组件

板载业务数据通常通过 `Sensors.h`、`INS.h` 获取；BMI088 硬件驱动及引脚初始化属于私有实现。

`IMUProtocol.h` 是外部 IMU 的 CAN 协议适配，使用 `SRNOS_IMUProtocol*` 类型/函数和 `SRNOS_CANDeviceFrame`；父项目显式持有实例并注册 CAN 适配。它不取代板载 INS。

`mahony_filter.h` 提供 C 链接的 `SRNOS_MahonyState`、`SRNOS_Axis3f` 和 `SRNOS_Mahony*` 函数；`quaternion_ekf.hpp` 位于 `SRNOS::IMU`，使用 SDK 自有数学组件。算法不创建线程，调用者负责实际 dt、单位、坐标系和输入有效性。

本轮保持计算及协议，EKF 主机套件和适配编译通过；板载 INS 回归由服务套件和固件检查覆盖，未新增算法精度或时序的板测结论。
