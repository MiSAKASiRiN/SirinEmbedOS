# SDK 验证方法与证据索引

验证先明确要证明什么，再选择主机、构建或板端方法。修改记录、测试结论和当时现场状态统一写入对应 `CHANGELOG.md`；[历史总表](Validation/CHANGELOG.md)保存已有跨模块记录，本页维护当前执行方法。

## 按验证层次选择入口

| 层次 | 执行内容 | 能证明的范围 |
|---|---|---|
| 源码与公共边界 | 命名、装配、公共头闭包、单一回调所有权 | 接口与目录合同，不代表代码在 MCU 上运行 |
| 主机测试 | 队列、状态机、协议、故障注入、并发模型 | 模型覆盖的行为，不代表电气时序或真实抢占 |
| 目标构建 | Debug/Release、父项目、最终 ELF 和链接布局 | 工具链、符号、容量与段布局，不代表实时栈水位 |
| 板端启动 | 配置冻结、Fault、USB/传感器状态、堆和任务 | 该镜像和该现场的启动情况，不代表满载与精度 |
| 连续负载 | 请求/实际速率、序号、错误、CPU、周期和时延 | 指定接线与窗口的性能；中断窗口与过载单独记录 |
| 物理验收 | 示波器、外部真值、掉电/断线及真实应用 | 仅限实际测量条件，不能由模拟或短测替代 |

## 离线执行示例

下面命令从 SDK 根目录执行。CMake、Ninja、Python 3 和 ST Arm Clang 的安装及最小构建见 [QuickStart](../QuickStart.md)。这些步骤不连接硬件。

```sh
python3 Tests/run_host_tests.py
python3 Tests/Build/check_sdk.py --public-headers-only
python3 Tests/Build/check_runtime_names.py --source-only
python3 Tools/Debug/check_i2c_timing.py
cmake --preset Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build --preset Debug
python3 Tests/Build/check_structure.py --compile-commands build/Debug/compile_commands.json
python3 Tests/Build/check_runtime_names.py --compile-commands build/Debug/compile_commands.json
python3 Tests/Core/check_foundation_elf.py build/Debug/SirinEmbedOS.elf
```

公共接口、工具链或配置合同变化时再运行完整 `python3 Tests/Build/check_sdk.py` 父项目矩阵。模块局部变更可先运行相应 `run_host_tests.sh`，例如 `sh Tests/Services/Network/run_host_tests.sh`；命令和覆盖范围见 [Tests/Build](../Tests/Build/README.md)。编译数据库和对象必须来自同一份当前构建，不能拿历史成功日志代替本次结果。

## 板端使用入口

- 初次启动、堆与任务快照：[BringUp](../Examples/BringUp/README.md)。栈水位未测量标记不能解释为零剩余。
- 原始双 IMU、环境器件与串口采集：[SensorBringUp](../Examples/SensorBringUp/README.md)、[HostLink CLI](../Tools/HostLink/README.md)。
- 正式姿态、温控与质量标志：[INSBringUp](../Examples/INSBringUp/README.md)。
- 托管任务生命周期、统计及 ADC 注入：[ManagedTasks](../Tests/Board/ManagedTasks/README.md)。
- CAN Classic、FD、共享 SPI：[CANThroughput](../Tests/Board/CANThroughput/README.md)、[CANFDLoad](../Tests/Board/CANFDLoad/README.md)、[CAN4SPILoad](../Tests/Board/CAN4SPILoad/README.md)。
- EEPROM 与持久化：[EEPROMBringUp](../Examples/EEPROMBringUp/README.md)、[Persistence](../Tests/Board/Persistence/README.md)。写入前保管外部备份；恢复结果单独核验。
- 转台校准：[IMUTurntableBringUp](../Examples/IMUTurntableBringUp/README.md)。运动、保存与安全支撑按该示例合同执行。

开始前确认板型、接线、探针/串口所有权和实际运行镜像。采样窗口内不暂停、不做全任务栈扫描；需要停止取证时记录边界。结束时明确镜像是否恢复、MCU 是否运行、断点和连接是否保留；旧记录中的“已恢复/运行”不是当前状态。

## 证据阅读与归档

每组结果应能定位到固件 ELF/二进制 SHA、配置、工具版本、接线、请求负载、实际时间窗、原始计数和判定方法。summary 是索引，原始日志、JSON/CSV 或 RAM 转储用于复核。失败、中止、中间镜像及未覆盖项与最终窗口分开保存。地址区间、单位、栈字/字节和 CPU ppm/% 必须明确。

只读查看示例：

```sh
python3 -m json.tool Docs/Validation/CAN4-Optimization-20261006/acceptance.json
rg --files Docs/Validation/CAN4-Optimization-20261006
```

重算二进制解码时先复制到新 `build/` 目录；部分解码脚本会在输入旁写 JSON，不要直接覆盖历史证据。新的结果写对应 `CHANGELOG.md` 并链接原始文件；README 保持阅读/复现指南，不继续追加日期战报。

## 证据目录

每个入口先说明数据口径和只读查看方法，对应 `CHANGELOG.md` 保存原报告、测量值及限制。

- [2613-20260926 原始证据](Validation/2613-20260926/)
- [BoardSupport-20260928](Validation/BoardSupport-20260928/README.md)
- [CAN13-ADC-20260928](Validation/CAN13-ADC-20260928/README.md)
- [CAN4-Optimization-20261006](Validation/CAN4-Optimization-20261006/README.md)
- [CAN4-SPI-20261006](Validation/CAN4-SPI-20261006/README.md)
- [CANClock-20261012](Validation/CANClock-20261012/README.md)
- [CANFD-20261003](Validation/CANFD-20261003/README.md)
- [CANFD-Board-20261006](Validation/CANFD-Board-20261006/README.md)
- [CANRouter-20261002](Validation/CANRouter-20261002/README.md)
- [CubeMX-20260928](Validation/CubeMX-20260928/README.md)
- [EEPROM-20260928](Validation/EEPROM-20260928/README.md)
- [I2C-20260928](Validation/I2C-20260928/README.md)
- [I2C400-20260928](Validation/I2C400-20260928/README.md)
- [IMUTurntable-20261009](Validation/IMUTurntable-20261009/README.md)
- [IMUTurntableAxes-20261010](Validation/IMUTurntableAxes-20261010/README.md)
- [IMUTurntableDirect-20261009](Validation/IMUTurntableDirect-20261009/README.md)
- [IMUTurntableFast-20261009](Validation/IMUTurntableFast-20261009/README.md)
- [IMUTurntableFullRestore-20261010](Validation/IMUTurntableFullRestore-20261010/README.md)
- [IMUTurntableFullSweep-20261009](Validation/IMUTurntableFullSweep-20261009/README.md)
- [IMUTurntableGravity-20261010](Validation/IMUTurntableGravity-20261010/README.md)
- [IMUTurntableINS-20261010](Validation/IMUTurntableINS-20261010/README.md)
- [IMUTurntableLatency-20261010](Validation/IMUTurntableLatency-20261010/README.md)
- [IMUTurntableLevelLanding-20261010](Validation/IMUTurntableLevelLanding-20261010/README.md)
- [IMUTurntableMotion-20261009](Validation/IMUTurntableMotion-20261009/README.md)
- [IMUTurntableSaveFault-20261010](Validation/IMUTurntableSaveFault-20261010/README.md)
- [IMUTurntableSlowRepeat-20261010](Validation/IMUTurntableSlowRepeat-20261010/README.md)
- [IMUTurntableStudio-20261010](Validation/IMUTurntableStudio-20261010/README.md)
- [ITCM-20260929](Validation/ITCM-20260929/README.md)
- [ManagedTasks-20260928](Validation/ManagedTasks-20260928/README.md)
- [ManagedTasksBoard-20260928](Validation/ManagedTasksBoard-20260928/README.md)
- [Persistence-20260928](Validation/Persistence-20260928/README.md)
- [PersistenceBoard-20260929](Validation/PersistenceBoard-20260929/README.md)
- [Priority-20261003](Validation/Priority-20261003/README.md)
- [SRNOSBoard-20260928](Validation/SRNOSBoard-20260928/README.md)
- [SRNOSBoundary-20260928](Validation/SRNOSBoundary-20260928/README.md)
- [StaticIMU-20260929](Validation/StaticIMU-20260929/README.md)
- [StaticThreshold-20260929](Validation/StaticThreshold-20260929/README.md)
- [SystemMonitor-20260928](Validation/SystemMonitor-20260928/README.md)
- [ins-20260928 原始证据](Validation/ins-20260928/)
- [telemetry-20260927 原始证据](Validation/telemetry-20260927/)
