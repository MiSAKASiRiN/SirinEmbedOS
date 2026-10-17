# 固定传感器采集

本目录负责 LSM6DS3、LIS2MDL、SPA06 的寄存器、转换及采集，并接入 BMI088 驱动。应用只包含 `SirinEmbedOS/Devices/Sensors.h`；任务装配由 Kernel 管理，硬件绑定与坐标关系由 Board/Support 管理，姿态融合和恒温控制由 INS 服务管理。配置和数据单位见 [Sensors 指南](../../../Docs/Sensors.md)。

BMI/LSM 通过 SPI4 采集，默认 1 ms 周期、优先级 48。LIS/SPA 共用优先级 24、2 KiB 栈的环境任务，默认 10 ms 轮询；器件数据就绪才发布样本，输出频率不等于轮询频率。

LIS/SPA 在 I2C4 各有保留槽，使用 30 ms 完成等待覆盖排队与传输；EEPROM 占用总线时可以排队。真正的 NACK、超时或硬件错误进入离线/重初始化流程，其他任务不能绕过设备层访问相同地址。

## 读取样本

以下任务侧片段取得调用者拥有的快照，检查状态、字段和采样时间。时间阈值由业务选择，不能仅凭系统 RUNNING 判断样本可用。

```c
#include "SirinEmbedOS/Devices/Sensors.h"
#include "SirinEmbedOS/Kernel/Thread.h"

bool read_bmi(SRNOS_IMUSample *sample) {
    const uint32_t needed = SRNOS_SENSOR_ACCEL | SRNOS_SENSOR_GYRO;
    if (!SRNOS_SensorGetIMU(SRNOS_SENSOR_BMI088, sample)) return false;
    if (sample->header.state != SRNOS_SENSOR_READY ||
        (sample->header.valid_fields & needed) != needed) return false;
    const uint64_t now_us = SRNOS_TimeUs();
    return now_us >= sample->header.acquisition_end_us &&
           now_us - sample->header.acquisition_end_us <= 2000u;
}
```

`SRNOS_SensorGetBMI088SPIStatistics()` 返回两颗片选的 SPI 统计副本，不要求包含驱动私有头。采样 observer 在配置阶段注册，只能有界复制或通知，不能在高优先级采样路径打印或阻塞。

采样单轮和等待由 Kernel 托管；普通等待不计入 step，处理中的 I/O、抢占和中断计入墙钟。主机回归在 SDK 根执行 `bash Tests/Devices/Sensors/run_host_tests.sh`，共享总线回归位于 `Tests/IO/I2C`。硬件连续性、温稳和姿态精度应分别验收，历史结果在 [CHANGELOG](CHANGELOG.md)，分层方法见 [验证指南](../../../Docs/Validation.md)。
