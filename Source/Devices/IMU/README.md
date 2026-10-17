# IMU 协议与算法组件

板载业务数据通过 [Sensors](../Sensors/README.md) 或 [INS](../../../Docs/INS.md) 获取；BMI088 寄存器、片选和硬件初始化属于私有驱动，应用不重复初始化。

`SirinEmbedOS/Devices/IMU/IMUProtocol.h` 提供外部 IMU 的 CAN 适配，使用 `SRNOS_IMUProtocol*` 与 `SRNOS_CANDeviceFrame`。父项目持有协议对象并注册 CAN；该协议不取代板载 INS。

需要独立融合时，可使用 `mahony_filter.h` 的 C 接口或 `quaternion_ekf.hpp` 的 `SRNOS::IMU` C++ 接口。算法不创建线程，调用者负责实际 dt、单位、坐标系和输入有效性。以下是独立 Mahony 初始化片段，不与 SDK INS 共享状态：

```c
#include "SirinEmbedOS/Devices/IMU/mahony_filter.h"
static struct SRNOS_MahonyState filter;

void init_local_filter(void) {
    SRNOS_MahonyInit(&filter, 1.0f, 0.0f, 0.001f); // 示例增益、1 ms 固定步长
}
void update_local_filter(SRNOS_Axis3f gyro_rad_s, SRNOS_Axis3f accel_m_s2) {
    SRNOS_MahonyInput(&filter, gyro_rad_s, accel_m_s2);
    SRNOS_MahonyUpdate(&filter);
    SRNOS_MahonyOutput(&filter);
}
```

步长必须与真实采样周期相符；示例不包含姿态有效性、标定、温控和掉线策略。常规应用应优先使用已有 INS 服务。算法与接口检查不能证明板端精度或时序，验证方法见 [验证指南](../../../Docs/Validation.md)。
