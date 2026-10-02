# 固定传感器采集

本目录负责固定 LSM6DS3、LIS2MDL、SPA06 的寄存器初始化、转换及采集，并接入 BMI088 驱动；公开接口为 `SirinEmbedOS/Devices/Sensors.h`。任务装配在 Kernel，板级绑定及坐标关系在 Board/Support。姿态融合和恒温控制属于 INS 服务。完整数据、状态与初始化合同见 [Sensors](../../../Docs/Sensors.md)。

BMI/LSM 通过 SPI4 dispatcher 采集。LIS/SPA 共用 Normal / 2 KiB Environment 线程，默认 10 ms 轮询；器件只有数据就绪才发布样本，实际输出频率不等同于轮询频率。

LIS/SPA 的 I2C4 请求使用各自保留槽，在 EEPROM 背景读写期间可以排队。寄存器读写使用 30 ms 完成信号量等待，覆盖总线的周期排队期限和活动期限。不会因为 EEPROM 正占用总线立即判为设备离线；真正 NACK、超时或硬件错误仍走原有离线/重初始化路径。同一传感器地址不能从其他任务绕过设备层访问。

测试：`bash Tests/Devices/Sensors/run_host_tests.sh`（转换、寄存器状态机及 worker），共享总线调度测试位于 `Tests/IO/I2C`；EEPROM 整片压力下的采样连续性见 [板端记录](../../../Docs/Validation/I2C-20260928/README.md)。

## 板级边界

固定绑定与坐标关系移入 Board/Support/Devices；采样、总线优先级和发布路径不变。

## 托管线程

BMI/LSM/环境采样拆为单轮和私有等待，统一 SRNOS_ 名称；保持周期超期重置及 BMI DMA 等待。 栈、优先级、资源所有权和常驻线程数保留。私有等待适配由 Kernel 驱动；普通等待不计入 step，处理期间 I/O/抢占/中断计入墙钟。统计不能替代板端时序验收。详见根文档及 Source/Kernel/README.md。

## 公开依赖与观测

公共接口只含 SDK 类型，固定 BMI088 寄存器、初始化和 SPI 片选移为私有实现。父项目通过 `SRNOS_SensorGetBMI088SPIStatistics()` 获得两颗片选的 SPI 统计副本，不引用驱动私有头；其他采样快照与频率保持。名称/路径迁移见 `Docs/SDKMigration.md`。
