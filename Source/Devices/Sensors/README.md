# 固定传感器采集

本目录负责固定 LSM6DS3、LIS2MDL、SPA06 的寄存器初始化、转换及采集，并接入 BMI088 驱动；公开接口为 `SirinEmbedOS/Devices/Sensors.h`。任务装配在 Kernel，板级绑定及坐标关系在 Board/Glue。姿态融合和恒温控制属于 INS 服务。完整数据、状态与初始化合同见 [Sensors](../../../Docs/Sensors.md)。

BMI/LSM 通过 SPI4 dispatcher 采集。LIS/SPA 共用 Normal / 2 KiB Environment 线程，默认 10 ms 轮询；器件只有数据就绪才发布样本，实际输出频率不等同于轮询频率。

LIS/SPA 的 I2C4 请求使用各自保留槽，在 EEPROM 背景读写期间可以排队。寄存器读写使用 30 ms 完成信号量等待，覆盖总线的周期排队期限和活动期限。不会因为 EEPROM 正占用总线立即判为设备离线；真正 NACK、超时或硬件错误仍走原有离线/重初始化路径。同一传感器地址不能从其他任务绕过设备层访问。

测试：`bash Tests/Devices/Sensors/run_host_tests.sh`（转换、寄存器状态机及 worker），共享总线调度测试位于 `Tests/IO/I2C`；EEPROM 整片压力下的采样连续性见 [板端记录](../../../Docs/Validation/I2C-20260928/README.md)。
