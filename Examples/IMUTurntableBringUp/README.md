# IMU 转台 BringUp

当前 **revision24**：上电仍三轴零输出，保留原控制器；仅匹配Studio插件的“转台校准”启动有界自动任务。保持正式INS及60°C温控。revision24 启用既有 Low/4 KiB/1 Hz SystemMonitor，并使用 SDK 默认 USART1 Panic 输出；控制参数及虚拟轨迹禁用状态不变。revision23 修复保存请求触发 HostComm 栈溢出复位；运动与校准流程沿用 revision22。流程与协议见[转台校准](Calibration.md)。旧的固定2500raw下放已证实不合格，新任务取消、断连和正常完成均保持当前位置承重，不调用该下放过程。以下revision21及更早部分作为历史说明保留。


当前 **revision21** 用于上位机开发：`control.hpp` 的 `MotorControlEnabled=false` 暂时旁路原电机控制。保留全部控制实现及CAN3三个GM6020反馈/遥测，周期CAN命令恒为0；STOP请求也不会启动下放。无回位、调平或扫转。恢复运动须修改开关并重新构建烧录，当前不是HostLink可写参数。turntable.phase=0不代表INS未就绪；unwrapped/speed控制器派生字段标记无效，原始编码器、RPM、电流、温度照常观测。

正式INS与60°C温控开启，SDK自动注册原始BMI/LSM、INS姿态及环境流，示例不再重复注册。首次温稳/静止初始化前没有有效姿态包，上位机通过TELEMETRY service4/message9查询INS状态。订阅message1正文`<HIB>`：stream_id=0x0102、period_us=1000、enabled=1。姿态流为1kHz，包含wxyz四元数、roll/pitch/yaw（rad）、角速度、加速度、时间戳及质量标志。四元数为机器人到世界，默认机器人坐标等于板坐标，世界Z向上；yaw是相对航向。协议与记录布局见[HostLink](../../Docs/HostLink.md#ins-与环境流版本1)。

```sh
build/hostlink-venv/bin/python Tools/HostLink/imu_telemetry.py --port /dev/cu.usbmodem355C343932333 --streams ins --json
```

以下为保留的运动模式说明；revision21旁路时不执行。


原运动模式（当前暂时旁路）：上电回位后使用双IMU重力方向静止调平，板+Z朝上，ID1保留原航向参考；然后闭环保持，不自动执行旋转辨识。CAN3 Classic 1Mbit/s连接GM6020 ID1/2/3，反馈0x205/206/207，控制组0x1FF；电压正方向映射{-1,+1,+1}。

## 上电默认姿态

启动种子raw350/4764/5070来自旧默认位附近的重力测量，不是最终标定零位。先回ID3、ID1、ID2，再使用两颗IMU转换至SDK板坐标的归一化重力平均，200ms滤波并静止平均1.5秒。每轮位置目标最多修正1°；平均重力与板+Z夹角约0.25°以内后，将ID2/3当前角度设为本次home并保持。ID1仍以raw350保留航向参考；没有磁北或绝对航向校准。双IMU方向差超过3°、采样失效或调平90秒未完成时转为LevelHold，继续承重，不直接归零。

正式限位仍为ID3 raw4200→8191→0→2160，内缩端点4264/2096。首次位于4000..4199允许从重力机械支撑恢复，回位后结束启动豁免。ID1/2可跨圈。直接位置P4→速度PI，无虚拟轨迹；速度上限54/27/27°/s，电压±30000。ID1/3 PI100/100、20ms控制速度低通；ID2 PI200/100、100ms。ID3重力前馈3120cos(theta)+4240sin(theta)，theta以raw4200为零的实际角度（弧度），回算抗饱和5/s；正常运行输出双向40000raw/s。参数仅适用当前转台负载。

## 正常停止和下力

HostComm空请求service0x40/message1为锁存的**定时制动下力请求**，ACK只表示接收，不表示已下力。ID1/2在计时期间保持当前位置，ID3直接固定正向电压2500raw，持续8秒后全部轴归零；没有退出速度环、位置环、重力前馈、输出斜率或编码器落座确认。2500是GM6020电压指令，不是恒定力矩或电流。没有恢复虚拟轨迹。 **run22实测此参数下降过快：0.28秒到支撑邻域，IMU记录明显冲击；尚未通过缓降验证，不应将8秒定时视为受控落座。**

用户授权退出专用通道越过4200，下界4000及上界越界保护保留；反馈、温度、时序故障保护保留。编码器和双IMU继续记录，但不调整退出输出、不决定计时结束。phase34在revision20仅表示定时下力完成，不能证明已到机械支撑；从不同姿态、负载退出时8秒不能保证落座。此前revision17～19的落座检测/PI退出控制已移除。

反馈失联、温度、时序/看门狗、运行越界等故障的紧急归零仍独立有效。断电、MCU被暂停/复位、CAN中断时软件不能保证阻尼；必须先完成正常降落再结束固件或断电。Host脚本SIGKILL、主机掉电也不能保证发送停止请求。首次从旧立即归零版本升级需现场支撑。

## 构建与采集

```sh
cmake --build build/IMUTurntable27 -j 8
build/hostlink-venv/bin/python Examples/IMUTurntableBringUp/capture.py \
  --port /dev/cu.usbmodem355C343932333 --output build/turntable-new --seconds 180
```

端口按实际枚举核对。默认在调平Hold记录6秒后请求降落，等phase34确认定时下力后退出。`--stop`只执行定时制动下力并记录到确认；`--keep-holding`显式在成功采集后保留承重。Ctrl+C/SIGTERM在正常采集循环中请求降落并等待；通信异常只能尽力请求，必须看最终状态，不能把进程退出当作电机已下力。低于revision17拒绝新停止语义，避免旧固件立即归零。

32个SOURCE V2只读遥测源保持：每轴raw/angle/online/unwrapped/speed/voltage/rpm/current/temperature，加phase/fault/fault_axis/fault_measurement/sequence_revision。默认200Hz标量、online/temperature100Hz、revision10Hz，双IMU原始流各1kHz。公开speed为100ms低通，不是瞬时转速；Landed后enabled=false，unwrapped/speed无效并保留旧值，实际静止须看raw/IMU。各源逐项发布而非原子同步。

phase：0等待、1启动恢复、2/3/4回ID3/1/2、9调平保持、10故障、21静止调平、22调平未确认保持、32定时制动、33卸力、34定时结束零输出、35降落未完成保持。历史5..20扫转和100+坐标辨识状态保留，但默认不执行。fault枚举保持历史合同，运行超速保护仍停用。

`TurntableView`仍是1ms/优先级24/3072B托管任务。无新增任务、无运行期重绑定；INS融合/加热/参数加载/HostLink存储关闭。IMU通过公共SensorGetIMU读取，只用于本应用调平，不写EEPROM或SDK安装矩阵。

## 验证与历史

- [调平和受控落座](../../Docs/Validation/IMUTurntableLevelLanding-20261010/README.md)：本次实现与硬件证据。
- [多姿态轴向标定](../../Docs/Validation/IMUTurntableAxes-20261010/README.md)：外→内ID1→ID3→ID2，原参考raw350/4850/5140；不能将更新后的home直接代入旧q=0而不修正偏置。
- [重力补偿](../../Docs/Validation/IMUTurntableGravity-20261010/README.md)、[三轴全行程](../../Docs/Validation/IMUTurntableFullRestore-20261010/README.md)：历史速度和行程结果。

`calibrate_axes.py`离线拟合双IMU轴向、机构顺序与联动验证，依赖NumPy/SciPy/Matplotlib；它使用旧辨识计划，不用于当前调平/落座采集。专项test_level_landing.cpp验证调平方向、限位和正常下降状态机，test_observation.cpp验证停止请求不立即触发CAN inhibit、故障看门狗仍归零。历史完整扫转虚拟植物测试不用于当前模式验收。

## 时延与 CPU 采集

`measure_latency.py --port <CDC1> --output <新目录>` 默认只观测。加 `--motion` 才启动已有校准动作；不保存 EEPROM，完成/异常尝试取消并保持承重，不调用已否决的定时下放。脚本只允许空闲或中止保持状态起步，状态 BUSY 可重试，运动命令不自动重发。独占 CDC1，输出原始帧、catalog、任务统计、任务状态和总结；15 个电机量各500 Hz、双IMU/INS各1 kHz、系统统计1 Hz。必须保持供电，软件退出后承重不代表允许断电。

`analyze_latency.py <采集目录>` 使用 NumPy/SciPy/Matplotlib，输出相对时延、CPU JSON 与 PNG；`--smoothing-ms 21/31/51` 可检查滤波敏感性。正时延表示第二信号较晚；只选独立分轴段，电机发布时间戳与IMU采集结束时间戳不等于物理总线时刻。电压/电流是闭环相关量，拟合弱时不输出可信延迟。CPU包含观测开销，循环流轮转发布，只对收到的窗口报告漏期结果。见 [实测及边界](../../Docs/Validation/IMUTurntableLatency-20261010/README.md)。
