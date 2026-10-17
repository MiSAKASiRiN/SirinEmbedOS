# IMU 转台 BringUp

此示例通过 CAN3 Classic 1 Mbit/s 接入三个 GM6020（ID 1/2/3），反馈为 0x205/0x206/0x207、控制组为 0x1FF。启动时持续发送零命令并发布反馈；只有匹配协议的显式校准启动请求才进入有界运动任务。`control.hpp` 的 `MotorControlEnabled=false` 不会禁止已启动的 `CalibrationJob`。协议版本由只读 `turntable.sequence_revision` 查询，任务配方见 [Calibration.md](Calibration.md)。

正式 INS、双 IMU/环境采样、60°C 温控及 SystemMonitor 启用；业务参数启动加载与通用 HostLink 存储命令关闭。校准保存使用本示例专用协议，仍可写 EEPROM。启动旁路不表示温控也关闭。

## 构建与只读观测

在 SDK 根目录执行。ST Arm Clang、CMake 与 Ninja 先放入 PATH：

```sh
cmake -S . -B build/IMUTurntableBringUp -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=CMake/starm-clang.cmake \
  -DCMAKE_BUILD_TYPE=Debug -DSIRINEMBEDOS_EXAMPLE=IMUTurntableBringUp
cmake --build build/IMUTurntableBringUp
```

产物为 `build/IMUTurntableBringUp/SirinEmbedOS.elf`。下载前核对实际转台接线、负载、镜像和当前支撑状态；编译不会连接或启动电机。

先按 [HostLink CLI](../../Tools/HostLink/README.md) 安装环境并确定独占 CDC1，然后只观察姿态：

```sh
build/hostlink-venv/bin/python Tools/HostLink/imu_telemetry.py \
  --port CDC1_PATH --streams ins --json
```

INS 温稳/静止初始化期间可能没有有效姿态包；查询质量标志、阶段及 epoch。四元数为机器人到世界、wxyz 顺序，欧拉角单位 rad，yaw 为相对航向。应用 `turntable.phase=0` 与 INS 就绪状态不是同一字段。

## 校准任务与停止语义

使用 [专用配方与 HostLink 协议](Calibration.md) 查询能力、显式启动、心跳、取记录及保存。正常取消、断连超时和完成均保持当前位置承重。`service=0x40/message=1` 只请求取消保持；ACK 不等于电机已归零或机械已落座。

反馈失联、电机故障和时序看门狗仍走独立故障归零。断电、暂停/复位 MCU 或 CAN 失效不能保证承重；结束前现场安全支撑，保持动作完成不等于允许断电。源码保留的固定输出下放路径不被当前校准协议调用。

`capture.py` 保留旧运动/下放采集逻辑，其中 `--stop` 和 phase34 等待不适合当前取消保持协议。当前采集使用下述 `measure_latency.py` 或通用 HostLink 工具；旧脚本及历史修订的适用范围见 [CHANGELOG](CHANGELOG.md)。

## 控制和遥测合同

单个 `TurntableView` 托管任务按 1 ms、优先级 24、3072 B 栈推进控制与任务统计。回调每轮返回，不另建无限循环；只通过公共 Sensor/CAN/HostLink 接口访问 SDK。

控制参数仅适用该转台：默认接近位置 raw350/4764/5070、ID3 合法弧段 4200→8191→0→2160；启动恢复范围从 raw4000 起。速度限幅 54/27/27°/s，电压限幅 ±30000 协议单位，电压正方向映射 −1/+1/+1。这些值不能直接解释为电流或力矩，也不是其他负载的通用参数。

32 个 SOURCE V2 只读源包含每轴 raw/angle/online/unwrapped/speed/voltage/rpm/current/temperature 以及 phase/fault/fault_axis/fault_measurement/sequence_revision。源逐项发布，不能当作原子同步快照；unwrapped/speed 在控制未启用或故障时可能无效，实际静止还需看编码器与 IMU。

## 时延和 CPU 采集

下列命令默认只观测，不启动运动、不保存 EEPROM；输出目录必须不存在：

```sh
build/hostlink-venv/bin/python Examples/IMUTurntableBringUp/measure_latency.py \
  --port CDC1_PATH --output build/turntable-observe --timeout 60
```

`--motion` 才启动现有校准动作；该选项需要满足配方的接线、支撑和温稳条件。运动请求不自动重发，结束/异常尝试取消保持，独占 CDC1。原始电机量、双 IMU/INS、任务统计和任务状态分别保存在输出目录。

已有采集可离线分析（NumPy/SciPy/Matplotlib）：

```sh
python3 Examples/IMUTurntableBringUp/analyze_latency.py build/turntable-observe
```

分析使用设备时间戳，正时延表示第二信号较晚；只比较独立分轴段，不能把发布时间差当作物理总线单程延迟。观测模式没有运动时，可能没有可辨识的时延窗口，CPU 仍含采集开销。`--smoothing-ms 21/31/51` 可分别检查滤波敏感性。

离线例程回归：`sh Tests/Examples/IMUTurntableBringUp/run_host_tests.sh`。具体修改、各次实测、失败窗口和未覆盖项统一见 [CHANGELOG](CHANGELOG.md) 及[验证索引](../../Docs/Validation.md)。
