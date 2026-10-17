# SensorBringUp

2613 新器件可达性与 SDK 驱动验证入口。关闭正式融合，启用 BMI 原始采样、LSM、LIS2MDL、SPA06，保持 MCP 服务运行；不主动发送外部 CAN 测试帧。关闭业务参数启动加载、HostLink SAVE/LOAD/静态校准命令和周期诊断扫描；原始采样不执行融合或应用校准系数；独立启用加热，优先 LSM 反馈目标60°C，LSM失效时回退BMI。CDC1 启用订阅式双 IMU 二进制遥测，CDC0 保持每秒摘要。FT24C128A 及 Storage 已接入。本例未关闭 storage.enabled，启动仍会读取身份/IMU 校准记录，原始数据不套用校准；没有自动写入。存储开关见 [存储总览](../../Docs/Storage.md)。

```sh
export PATH="/opt/ST/STM32CubeCLT_1.22.0/st-arm-clang/bin:$PATH"
cmake -S . -B build/SensorBringUp -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=CMake/starm-clang.cmake \
  -DCMAKE_BUILD_TYPE=Debug -DSIRINEMBEDOS_EXAMPLE=SensorBringUp
cmake --build build/SensorBringUp -j 8
```

以上命令在 SDK 根目录执行；产物 `build/SensorBringUp/SirinEmbedOS.elf`。Release 使用另一个构建目录。CLion 从 SDK 根打开，为该示例建立独立 CMake profile 和对应 ELF 调试配置。下载、复位会改变目标现场，连接前先检查已有 GDB/J-Link 所有者。

公共调用合同见 [Sensors.md](../../Docs/Sensors.md)。调试器暂停后仅在 `srnos_sensor_bringup.sampling == false` 时解释整份快照；每秒发布状态、样本、堆余量及 SPI 统计，每 10 秒更新 `window_samples/window_polls/window_us`。速率为 `window_samples[i] * 1e6 / window_us`，设备顺序 BMI/LSM/LIS/SPA。任务轮询次数不等于有效样本数。

每秒日志只输出摘要，不逐样本打印。USB 启动不代表日志已到主机；未枚举时用 RAM 快照。实时窗口中不要打断点或进行全任务栈扫描。需要栈余量时可在窗口前后暂停，由主机读取 RAM 后计算填充水位：

```gdb
source Tools/Debug/sensor_snapshot.gdb
srnos-sensor-snapshot
srnos-sensor-stacks /tmp/sensor-before
continue
```

GDB 已开启文本 logging 时，可在主机运行 `python3 Tools/Debug/read_stack_dump.py GDB_LOG /tmp/sensor-before`。该脚本不在 MCU 执行函数。暂停本身仍扰动实时性，不能把前后暂停时间计入无扰动窗口。

启动时第一灯黄色、蜂鸣器上行三音；RTOS 的 Indicators 线程随后让黄色灯亮/灭各 0.5 秒（1 Hz）。SensorBringUp 开启 LSM 优先、BMI 回退的60°C加热，BringUp 仍关闭。LSM 温度独立显示，不保证等于 BMI 温度。实际声光效果需要现场确认，驱动计数并不能替代观察。

使用 `Tools/HostLink/imu_telemetry.py --port CDC1_PATH --duration 600` 进行用户长测；Python 环境安装、完整协议、计数解释见 [HostLink.md](../../Docs/HostLink.md)。订阅前不发送 IMU 数据，退出取消订阅。不要同时让串口终端占用 CDC1。

## 任务接入

应用使用托管 `step` 回调，周期等待由 SDK 负责。检查 `srnos_example_create_result` 确认任务创建结果；创建、停止、删除与同步对象用法见 [Kernel](../../Source/Kernel/README.md)。

修改与验证记录见 [CHANGELOG](CHANGELOG.md)。
