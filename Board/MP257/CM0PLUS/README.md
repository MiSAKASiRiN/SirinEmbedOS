# ATK-DLMP257B 的 M0+ W.Bus 接收

Cortex-M0+ 使用 LPUART1 接收 W.Bus，按 100000 bit/s、8E2、RX 反相配置，只启用接收。UART 中断调用 SDK 的 `SRNOS_WBusFramerFeed`，裸机主循环每 50 ms 发布一次供 A35 读取的诊断。时钟配置来自配套 A35-TD 设备树，CPU3 为 200 MHz，LPUART1 内核时钟为 16 MHz。

## 工程入口与数据流

从 SDK 根目录选择 `MP257-M0Plus` 预设；[子目标清单](CMakeLists.txt) 组装 CMSIS 启动代码、[默认裸机主循环](main.c)、[接收与中断入口](receiver.c)、[W.Bus 帧解析](../../../Source/Services/RemoteControl/WBus/wbus_framer.c) 以及[跨核快照](../IPC/peer.c)。[链接脚本](remoteproc.ld) 划分代码、双向快照、数据和栈。

独立父项目可选用子模块中的 `CMake/mp257-m0plus.cmake`、调用 `add_subdirectory(SirinEmbedOS)` 和 `sirinos_add_firmware(target App/main.c)`。应用持有裸机 `main()`，包含 `SirinEmbedOS/IO/MP257/WBusReceiver.h`，启动时调用一次 `SRNOS_MP257WBusReceiverInit()`，随后在开中断的主循环中反复调用 `SRNOS_MP257WBusReceiverWaitAndPoll()`。该函数等待一次中断并处理到期工作。

| 执行位置 | 更新时机 | 内容 |
|---|---|---|
| LPUART1 接收中断 | 收到字节，每次 IRQ 最多处理 16 次 | 检查 UART 错误并送入组帧器；成功解出完整帧后立即发布 16 路通道、D17/D18、lost、failsafe、帧计数和时间 |
| 裸机主循环 | SysTick 为 1 ms，每个新 tick 轮询一次下行 | 处理最新 UART 请求和 TTL；当前下行保存在内存供回读，LPUART1 仅启用 RX |
| 低频诊断 | 每 50 ms 刷新 trace，每 100 ms 发布 IPC 状态 | 提供帧龄、串口计数、错误与丢弃计数，便于区分接线、解码和遥控失联问题 |

A35 通过 `/dev/srnos-m0` 访问 LPSRAM `0x200c3c00` 的 1 KiB 保留窗口。使用 `sudo mp257-ipc radio` 查看最新通道和帧标志，`sudo mp257-ipc status m0` 查看低频计数。`age_ms`、`lost`、`failsafe` 由读取方共同判断。M0+ 只发布遥控数据；遥控转为 M33 电机命令需要 A35 应用显式转发。

## 板级接线与资源

| 连接 | ATK-DLMP257B |
| --- | --- |
| RF206S 的 W.BUS 信号输出 | J9-21，丝印 MISO，PZ1 / AF6 / LPUART1_RX |
| 接收机 GND | J9-20，板地 |

接收机独立供电，信号电平应与板端兼容。PZ1 与 W25Q128 的 SO 共享。`wbus.dtso` 禁用 Linux SPI8，将 PZ3 配成高电平 GPIO hog，并将带弱下拉的 PZ1 交给 M0+ remoteproc 的默认 pinctrl。使用此启动项时，Linux 无法访问这颗 NOR Flash。GPIO hog 从 Linux 探测 GPIOZ 起持有片选；更早的复位和引导阶段电平取决于板上电路及引导固件。

## 构建与加载

从 SDK 根目录使用 ST Arm Clang、Ninja 与 STM32CubeMP2 V1.3.0。可以通过 `STM32CUBE_MP2_ROOT` 指定本地 Cube 包。

```sh
cmake --preset MP257-M0Plus
cmake --build --preset MP257-M0Plus
```

产物为 `build/MP257-M0Plus/Board/MP257/CM0PLUS/SirinEmbedOS-MP257-M0Plus.elf`。向 Linux 部署前检查该板 M0+ 保留内存包含从 `0x200c0000` 开始的 16 KiB 代码区，以及从 `0x200c4000` 开始的 4 KiB 数据区。固件使用 SDK 自有链接脚本、CubeMP2 的 CMSIS 启动与系统文件。

对带有 `__symbols__` 的本板基础 DTB，可在开发机合并片段。下面的 `original.dtb` 指当前可启动的完整 DTB。

```sh
dtc -@ -I dts -O dtb -o wbus.dtbo Board/MP257/CM0PLUS/wbus.dtso
fdtoverlay -i original.dtb -o wbus.dtb wbus.dtbo
```

将合成的 DTB 放到独立启动项并保留原项。安装 ELF 到板端 `/lib/firmware/` 后，在板端运行以下命令。示例使用本板实测的 `remoteproc1`，启动前核对名称和状态。

```sh
r=/sys/class/remoteproc/remoteproc1
test "$(cat "$r/name")" = m0 && test "$(cat "$r/state")" = offline &&
    echo SirinEmbedOS-MP257-M0Plus.elf | sudo tee "$r/firmware" &&
    echo start | sudo tee "$r/state"
```

M0+ 固件按需启动。停机时 Linux 使用 IPCC2 硬件通道 3 发送 shutdown，M0+ 从处理器端点 C1 向 A35 的 C2 应答。

## 查看实时数据

二进制上行随每个 W.Bus 帧更新，A35 可使用 `sudo mp257-ipc radio`。固件区末尾的 1 KiB 预留给双向快照，代码上限为 15 KiB。下行数据可通过状态回显观察。详见 [共享内存指南](../IPC/README.md)。

在板端执行以下命令。通过 SSH 使用 `watch` 时为会话分配终端。

```sh
sudo watch -n 0.2 cat /sys/kernel/debug/remoteproc/remoteproc1/trace0
```

`WAITING` 表示启动后尚未解出有效帧。`RECEIVING` 表示最近 100 ms 内有有效帧，`STALE` 会保留最后一次通道快照，结合 `last_frame_age_ms` 判断时效。CH1～16 为 11 位原始值；D17、D18、lost、failsafe 均来自最新帧。帧计数包含带 lost 或 failsafe 标志的帧。UART 错误会丢弃受影响的数据，超过 30 ms 的字节间隔会清除未完成的帧。trace0 用于人工采样，可跳过中间帧。
