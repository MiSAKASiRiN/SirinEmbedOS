# ATK-DLMP257 上的 SirinEmbedOS

同板 Cortex-M0+ 的独立构建、接线及通道观测见 [M0+ W.Bus](CM0PLUS/README.md)。A35 双向数据接口及 M33 目标验证见 [共享内存](IPC/README.md)。可选闭环应用使用同一 1 ms IPC 任务和 SDK PID，默认 Minimal 固件维持回显行为。

该目标运行在 STM32MP257D 的非安全 Cortex-M33 上。A35-TD Linux 通过 remoteproc 加载 ELF。应用使用公共 Thread、Sync、Classic CAN Router 和电机协议接口。独立构建使用仓库原有的 Minimal 应用，启动一个周期线程，默认发送的 CAN 帧数为零。

## 构建

需要 CMake、Ninja、ST Arm Clang/Picolibc 和 STM32CubeMP2 V1.3.0。Cube 包提供 CMSIS 启动代码、MP2 HAL 和同版本 FreeRTOS 的 ARM_CM33_NTZ port；内核、CMSIS-RTOS2 wrapper 和 heap_5 使用 SDK 快照。可用环境变量或 CMake 缓存变量 `STM32CUBE_MP2_ROOT` 指定软件包根目录。

```sh
cmake --preset MP257-M33
cmake --build --preset MP257-M33
```

默认产物是 `build/MP257-M33/Board/MP257/SirinEmbedOS-MP257.elf`。父项目在顶层选用 `CMake/mp257-m33.cmake` 工具链，通过 `add_subdirectory()` 引入 SDK，再调用 `sirinos_add_firmware(target app.cpp)`。目标会向应用传播三路 CAN 的编译期配置。所有参与链接的应用对象均须使用同一配置重新编译。

## M33 工程入口与数据流

| 层次 | 源码 | 启动后执行的工作 |
|---|---|---|
| 平台启动 | [boot.c](boot.c) | 以默认 Classic CAN 配置调用父项目的 `SRNOS_ApplicationConfigure`，校验并冻结注册，再建立 FreeRTOS/CMSIS 线程 |
| 总线 | [can.c](can.c) | 初始化 FDCAN 和 GPIO，三路 RX/TX worker 通过公共 Router 收发 |
| 达妙反馈 | [DM.cpp](../../Source/Devices/Motors/DM/DM.cpp) | 在所属 CAN 通道匹配反馈 SID 和载荷 ID，按实例量程解码，由 `feedback()` 提供包含连续位置的一致快照 |
| A35 通信 | [IPC/peer.c](IPC/peer.c) | 1 ms 任务轮询下行并发布电机槽，100 ms 发布 CAN 就绪位、RX 和 TX 完成计数 |

电机接线和注册表由独立父工程维护。注册动作只建立路由。注册结果、驱动器是否在线以及电机是否使能应分别确认。

A35 与 M33 通过 DDR 的 `0x81201000` 窗口交换最新快照。板端只读命令 `sudo mp257-ipc status m33` 查看三路总线计数，`sudo mp257-ipc motor` 查看 IPC 目标和过期情况。使用默认空控制回调时，profile 为零；逐轴达妙反馈保存在 M33 实例中。A35 与 M0+ 的遥控链路另见 [M0+ 实现](CM0PLUS/README.md)。

## remoteproc

在开发机上传镜像，下列命令中的 `board` 代表板子的 SSH 主机名。

```sh
scp build/MP257-M33/Board/MP257/SirinEmbedOS-MP257.elf board:/tmp/
ssh board
```

在板端确认 M33 当前为 offline，再安装并启动镜像。选择已有 firmware 或停核运行中的应用时，先确认其所有者。

```sh
sudo sh -eu <<'SH'
d=/sys/class/remoteproc/remoteproc0
test "$(cat "$d/name")" = m33
test "$(cat "$d/state")" = offline
install -m 644 /tmp/SirinEmbedOS-MP257.elf /lib/firmware/SirinEmbedOS-MP257.elf
echo SirinEmbedOS-MP257.elf > "$d/firmware"
echo start > "$d/state"
cat "$d/state"
SH
sudo cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
```

每秒更新的 trace 给出 RTOS tick、微秒时钟、三路 ready、路由 RX 和 TX 完成计数。连续读取可以确认调度。手动停止命令为 `echo stop | sudo tee /sys/class/remoteproc/remoteproc0/state`。固件使用 IPCC1 第三通道响应 Linux 的 shutdown，停止 CAN 收发并释放所持 GPIO 信号量。Linux 侧的 ELF 和向量表约定见 [ST remoteproc 说明](https://wiki.st.com/stm32mpu/wiki/Linux_remoteproc_framework_overview)。

## CAN 和线程

| 通道 | TX | RX | 复用 |
| --- | --- | --- | --- |
| CAN1 | PB9 | PB11 | AF7 |
| CAN2 | PI9 | PI10 | AF4 |
| CAN3 | PD2 | PD1 | AF9 |

此配置要求 FDCAN 归 CID2，六个引脚允许 M33 配置。固件读取 FlexGen26 的实际频率并要求 100 MHz 和 CCU 分频 1。三路均为 Classic 1 Mbit/s，预分频 5，TSEG1 为 15，TSEG2 为 4，SJW 为 4，采样点 80%。每路在共享 Message RAM 中预留 256 个 32 位字，其中 RX FIFO、TX FIFO 和 TX event FIFO 共使用 208 个字。默认进入正常总线模式；单独构建时设置 `-DSIRINEMBEDOS_MP257_CAN_LOOPBACK=ON` 可使用三个控制器的内部回环。

在 `SRNOS_ApplicationConfigure()` 中注册接收回调、电机 adapter 和启动线程。运行时使用 `SRNOS_CANRouterPostOneshot()`、`SRNOS_ThreadCreate()` 等公共接口。三对 RX/TX worker 沿用 SDK 实现、实时优先级和各 4096 字节栈；FDCAN IT0 使用优先级 6。电机型号和业务行为由应用选择。[CAN Router](../../Source/IO/CAN/README.md) · [电机协议](../../Source/Devices/Motors/README.md) · [线程](../../Source/Kernel/README.md)

M33 使用 1 kHz SysTick、256 KiB heap_5、8 KiB MSP 和 8 KiB 独立 libc arena。向量位于 `0x80100000`，数据区位于 `0x80a00000`，资源表和 trace 使用 `0x81200000` 起始的 IPC 预留区。板级构建范围限于线程、同步和三路 Classic CAN。H723 的服务装配、运行统计采样、CAN4 和 CAN FD 属于其他目标。
