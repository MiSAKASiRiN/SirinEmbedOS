# CAN FD 公共接口示例

本示例只包含 SDK 公共头，将 CAN1 配置为 CAN FD：仲裁相位 1 Mbit/s、数据相位 4 Mbit/s、发送启用 BRS。CAN2/3/4 保持 Classic CAN 1 Mbit/s。配置由 `SRNOS_ApplicationConfigure()` 声明，初始化阶段冻结；不修改线程优先级，也不在运行期切换模式。

`bind_channels` 为 CAN1 注册扩展 ID `0x1FFF0123` 的精确匹配回调。回调在 CAN1 RX worker 中复制接收帧并递增计数，支持包括 0～8 B 在内的 FD 帧；不阻塞、不解析设备协议。Classic 帧不会进入此回调。其他业务协议应由父项目定义，现有 Classic 电机 adapter 不可绑定到 CAN1。

`SRNOS_ApplicationMain()` 读取冻结配置、系统启动状态及 FD 统计，然后返回。默认不主动发送，不创建额外任务；返回后 SDK 的 RX 回调继续有效。

原生 RX worker 每次从 ring 弹出最多 4 帧，每轮调度仍最多处理 32 帧，达到上限后保留通知并让出 CPU。此策略避免 32 帧暂存占满 4 KiB 线程栈；父项目回调仍须有界、非阻塞，并控制自身栈占用。

调试器可观察：

| 变量 | 含义 |
|---|---|
| `SRNOS_CANFDExampleBindResult` | 初始化订阅结果，失败会使 bind_channels 拒绝启动。 |
| `SRNOS_CANFDExampleConfigRead` / `Config` | 是否读到冻结配置及其副本。 |
| `SRNOS_CANFDExampleStatus` | ApplicationMain 时刻的系统状态；`native_can_ready` 是三路原生控制器的初始化结果，不是总线连通证明。 |
| `SRNOS_CANFDExampleStatsRead` / `Stats` | ApplicationMain 时刻的统计快照，不会自动刷新。 |
| `SRNOS_CANFDExampleRxCount` / `LastRx` | RX worker 更新的接收计数和最后帧，包含实际 BRS/ESI；仅供调试器观察，不是跨任务一致快照 API。 |
| `SRNOS_CANFDExamplePostAttempted` / `PostResult` | 可选单帧发送是否尝试以及软件入队结果。 |

## 可选单帧发送

父项目可对示例目标添加：

```cmake
sirinos_add_firmware(CANFDExample "${SDK_DIR}/Examples/CANFD/app_main.c")
target_compile_definitions(CANFDExample PRIVATE SRNOS_CANFD_EXAMPLE_SEND_ONCE=1)
```

此时 ApplicationMain 在原生初始化成功后，仅提交一次扩展 ID `0x1FFF0124`、12 B 的测试数据：ASCII `SRNOS-FD` 加 `01 02 03 04`；它没有 SDK 电机控制语义。测试 ID 由示例约定，实际接入时由父项目确认 ID 分配。发送 BRS 与冻结策略一致，优先级为 1，TTL 为 20 ms；`SRNOS_CANFD_OK` 只表示已复制到软件队列，不表示已发送或收到 ACK。软件 TTL 不会撤销已经进入硬件的帧，硬件可能按控制器重传策略继续尝试。

## 构建与验证范围

`Tests/Build/Parent` 的 `CANFDExample` 提供固件目标；`Tests/Build/check_sdk.py` 检查它的公共头隔离和最终 ELF。默认目标保持只接收，单帧开关可由父项目单独启用。

本示例不包含 CAN FD 业务协议或电机策略。主机/构建检查不证明收发器支持 4 Mbit/s、总线终端匹配、ACK 或目标吞吐；这些需要 PCB 在线后的实际 FD 对端验证。原生驱动和模式合同见 [CAN 模块](../../Source/IO/CAN/README.md) 及 [公共接口](../../Include/SirinEmbedOS/IO/CAN/FD/CANFD.h)。

修改与验证记录见 [CHANGELOG](CHANGELOG.md)。
