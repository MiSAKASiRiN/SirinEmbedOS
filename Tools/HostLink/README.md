# IMU / INS 遥测与采集 CLI

Python 3 + pyserial；必须显式指定 CDC1。安装、运行与完整协议见 [Docs/HostLink.md](../../Docs/HostLink.md)。

## 完整采集保存

`imu_capture.py` 保存每一条成功解码的样本，不是每秒终端摘要。在 SDK 根目录执行：

```sh
python3 -m venv build/hostlink-venv
build/hostlink-venv/bin/pip install -r Tools/HostLink/requirements.txt
build/hostlink-venv/bin/python Tools/HostLink/imu_capture.py \
  --port /dev/cu.usbmodem355C343932333 \
  --warmup 120 --duration 600 \
  --output captures/static-01 \
  --note "板子固定静置，加热关闭，环境可能轻微振动"
```

端口是本机此前 CDC1 路径，运行前核对当前枚举；不要选择 CDC0 日志口，也不要同时运行 monitor/其他串口程序。预热 120 秒也保存，随后测量 600 秒，总计约 12 分钟；已经预热可省略 `--warmup`。`--duration 0` 持续采集到 Ctrl-C。不修改固件、不写 EEPROM、不控制加热或重新配置传感器，预热只是记录阶段标记。

输出目录必须不存在，不会覆盖旧数据；省略 `--output` 时自动创建 `captures/imu-UTC时间/`。默认 captures 已从 Git 忽略；十分钟双 1 kHz 的 CSV 加原始字节应预留数百 MB 磁盘空间。

| 文件 | 内容 |
|---|---|
| `bmi088.csv`、`lsm6ds3.csv` | 逐样本序号、状态/有效位、设备采集起止 µs、主机收到该批数据的 monotonic ns、原始加速度/角速度/温度、m/s² / rad/s / °C，以及 phase |
| `cdc1_rx.bin` | 串口读取到的原始字节流，含握手、状态响应和遥测，支持按原 COBS/CRC 协议复查 |
| `status.jsonl` | 订阅前、每秒、取消订阅后的完整设备状态与累计计数；附主机时间 |
| `metadata.json` | 参数、环境备注、脚本/解码器哈希、HELLO 正文、结束原因、CSV 行数、序号缺口/重复/乱序、时间回退、协议错误、首尾设备状态及测量段接收率 |

`phase=warmup` 为预热，`measurement` 为正式采集，`stopping` 为超时或取消订阅时排空的尾部。分析默认只取 measurement。phase 根据主机收到数据的时间划分；频谱分析使用设备的 acquisition_start_us，不使用 USB 批次到达时间。时间戳保留整数精度，数据不插值、不补帧、不在主机额外旋转、不假设两颗 IMU 同步。双 IMU 原始流是传感器轴；INS 和环境流的 SI 向量已由固件转换至机器人轴，整数原始计数仍是器件轴。

每秒刷新文件缓冲，正常结束/Ctrl-C 时 fsync 保存并取消订阅。强制杀进程或断电可能留下尾部不完整行，metadata 会保持 in_progress；分析前必须核对。Ctrl-C 返回 130 并标记 interrupted，断连返回非零并保存失败原因。**不自动重连拼接**，复位或重新连接后创建新目录重新采集。

`complete=true` 只表示定时采集及收尾完成，不等于零丢样或测量质量合格；同时检查 `quality_issues`、CSV 时间/序号和 status 中设备计数。overflow/failed 为设备启动累计值，需对首尾做差；discarded 包括主动取消的尾部清理。CLI 状态每秒显示接收率、健康与丢样计数，不逐样本打印。

可选 `--elf PATH` 保存所提供文件的 SHA256，**不核对板上镜像**。遥测协议不提供完整 ODR/量程/滤波配置回读，脚本不会把当前源码配置冒充实测；原始采样可用 SensorBringUp，姿态采集须用 INSBringUp 或开启正式 INS 的应用；必要时在 `--note` 记录实际配置、板子方向及外界振动。

采集完成后，将输出目录路径交给分析工具即可做丢样检查、温漂、静置统计和频谱对比。轻微振动会进入测量，应在分析中分离周期成分与宽带噪声，不能直接把全部波动归为 IMU 本底噪声。

## 仅监视，不保存

```sh
python3 -m venv build/hostlink-venv
build/hostlink-venv/bin/pip install -r Tools/HostLink/requirements.txt
build/hostlink-venv/bin/python Tools/HostLink/imu_telemetry.py --port CDC1_PATH --duration 600
```

在 SDK 根目录执行。每秒输出最新全部数值，每十秒报告采集率/接收率及错误；Ctrl-C 取消订阅。不自动选端口、不写设备存储、不提供文件录制。重连会重新握手订阅。主机满缓冲测试与正常收包窗口分别评估。

## 正式 INS 与环境数据

```sh
build/hostlink-venv/bin/python Tools/HostLink/imu_telemetry.py \
  --port CDC1_PATH --streams ins mag pressure --duration 600
```

可加`bmi lsm`同时观察五条流，`--json`查看完整状态/十秒速率。INS未收敛时显示阶段而无姿态样本；温度、反馈来源、温稳与过温保护独立显示。四元数wxyz表示机器人到世界，欧拉角单位rad；原始双IMU仍是各自传感器轴。安装与启动要求见[INS](../../Docs/INS.md)。保存脚本默认仍采集两路原始数据，原 CSV 列不变；使用下面的 `--streams` 可额外保存正式 INS 与环境数据。


### 保存五路完整数据

先确认固件启用了正式 INS（SensorBringUp 不输出姿态），退出上面的监视程序，再执行：

```sh
build/hostlink-venv/bin/python -m serial.tools.list_ports -v
build/hostlink-venv/bin/python Tools/HostLink/imu_capture.py \
  --port CDC1_PATH --streams bmi lsm ins mag pressure \
  --warmup 120 --duration 600 --output captures/ins-static-01 \
  --note "固定静置，默认60°C；记录安装方向与环境振动"
```

`--streams` 支持任意组合；只保存姿态可用 `--streams ins`。已温稳收敛可设置 `--warmup 0`。预热时间不会等待服务就绪：若板子持续运动，低动态观测可能继续等待，INS CSV 可能没有样本，状态文件仍报告阶段。采集前确认运行状态，采集后核对 measurement 行数。

| 新文件 | 字段与解释 |
|---|---|
| `ins.csv` | sequence、epoch、flags、ins_phase、来源位/新输入位、attitude_us/published_us、两路输入序号/年龄/权重、四元数 wxyz、roll/pitch/yaw rad、机器人系 rad/s 与 m/s² |
| `lis2mdl.csv` | 原始磁场三轴/温度，机器人系磁场 µT 与 °C、有效位、采集起止时间 |
| `spa06.csv` | 原始压力/温度、补偿后 Pa 与 °C、有效位、采集起止时间 |

只创建所选流的 CSV。所有 CSV 的 `phase` 是主机采集阶段；`ins_phase` 才是固件 INS 状态。四元数表示机器人到世界；source_mask 位 0 为 BMI、位 1 为 LSM，输入字段也按此顺序。INS 没有独立的“姿态温度”；双 IMU 各自温度在对应 CSV，温控反馈/目标/PWM/来源/温稳在 `status.jsonl` 的 `device.ins_service`。环境流只发送新测量，不重采样到 1 kHz。

五路模式的 metadata 为 format_version=2，列定义在 `fields_by_stream`；保留 `fields` 作为旧原始 IMU 列定义。每个流有单独统计和时间基准；INS 的 `measurement_rate_hz` 用 attitude_us，其他流用采集开始时间。它是已收到样本按设备时间跨度计算的平均率，不等于固件计算率；worker 周期率从 `ins_service.cycles` / `updated_us` 的差分检查，有效姿态发布率从 `publications` 的差分检查，完整接收率同时核对行数、时间跨度与缺序。两次状态请求不是原子快照。

出现 `epoch_changes` 表示航向参考重新建立，需要分段分析。`complete=true` 并不保证质量，检查 `quality_issues`（包括没有姿态样本、epoch 变化、序号/时间问题、设备溢出等）。服务未就绪时无伪有效姿态；不能将剩余流持续输出当成 INS 正常。五流十分钟 CSV 与原始字节建议预留至少 1 GB 空间。
