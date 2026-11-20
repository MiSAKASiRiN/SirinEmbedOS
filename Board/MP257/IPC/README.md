# MP257 三核最新值通信

A35 上的 `srnos_ipc` 驱动分别连接 M33 和 M0+ 的 Linux remoteproc。每条链路有三个单写区域，分别保存上行快照、下行命令和低频状态。默认 M33 固件在内存中回显电机目标，带控制应用的固件另外接受显式预览和执行请求。UART 下行在 M0+ 内存中回显。J9-21 的物理接收路径照常工作。

| 设备 | 上行最新值 | 下行最新值 | 低频状态 |
|---|---|---|---|
| /dev/srnos-m33 | 每 1 ms 发布已采纳的 16 槽目标、有效掩码、采纳和过期计数 | 一组带有效位的有符号角度目标，单位毫度 | 每 100 ms 发布 CAN 就绪位、三路 RX 与发送完成计数 |
| /dev/srnos-m0 | 每个有效 W.Bus 帧发布 CH1～16、D17/D18、lost、failsafe 和帧时间 | 最多 64 字节，单独指定有效期 | 每 100 ms 发布 UART 及解码计数、当前有效的下行数据 |

M33 的观测字段 `dry_run_mdeg` 是被采纳的目标。当前 `measured_count` 为零。上层可独立结合 RF 帧的年龄、lost 和 failsafe 判断输入有效性。

## 构建与使用

使用 SDK 根目录的 `MP257-M33` 和 `MP257-M0Plus` 预设构建固件。Linux 模块针对运行内核的源码、配置及符号版本编译，使用该 BSP 的外部模块构建目录。示例在 Linux 源码工作区执行，当前目录为 SDK 根目录。

```sh
make -C "$KERNEL_BUILD" M="$PWD/Board/MP257/IPC/linux" modules
sudo install -D -m644 Board/MP257/IPC/linux/srnos_ipc.ko "/lib/modules/$(uname -r)/extra/srnos_ipc.ko"
sudo depmod -a
sudo modprobe srnos_ipc
sudo install -m755 Tools/MP257/mp257_ipc.py /usr/local/bin/mp257-ipc
```

将两份 ELF 复制到板端 `/lib/firmware/`，按 [M33](../README.md) 和 [M0+](../CM0PLUS/README.md) 的 remoteproc 流程选择固件并启动。为保留旧镜像，可将两份 ELF 分别命名为 `SirinEmbedOS-MP257-ipc.elf` 和 `SirinEmbedOS-MP257-M0Plus-ipc.elf`。本模块和固件均支持手动加载。

板端命令：

```sh
sudo mp257-ipc radio
sudo mp257-ipc status m0
sudo mp257-ipc status m33
sudo mp257-ipc motor
sudo mp257-ipc target --ttl 80 12000 -3000
sudo mp257-ipc uart --ttl 400 01020304
```

持续发布目标的应用应保持同一个可写文件描述符。每次单独运行命令行写操作都会建立新会话。使用 Python 模块时可以在同一个进程内完成发布及观察：

```python
import time
from mp257_ipc import endpoint, motor, send_targets

with endpoint("m33", writer=True) as fd:
    send_targets(fd, [12000, -3000], 80)
    time.sleep(0.01)
    print(motor(fd))
    time.sleep(0.10)
    print(motor(fd))
```

Python 示例从 `Tools/MP257` 导入。一个写会话可以有多个只读观察者。关闭写句柄后，最后一次写入仍按照原 TTL 到期；新写会话会先令旧目标失效。

## 快照与生命周期

[abi.h](abi.h) 定义小端、32 位对齐的布局。每个槽含一个发布标记和 252 字节载荷。写方置奇数、写载荷、置偶数并在各阶段之间执行写屏障。读方使用读屏障，在复制前后核对标记。实时核每周期最多尝试一次读取，包含旧值的槽位可直接覆盖。标记零保留作未发布。

A35 通过 `pread` 从偏移 0 获取上行快照，从偏移 256 获取状态，长度为 `sizeof(SRNOS_IpcRead)`。如果写方正在更新则返回 `EAGAIN`，尚未发布返回 `ENODATA`。驱动每次调用都在 remoteproc 生命周期锁内重新定位内存。用户空间只交换快照的本地副本。

每个端点只接受一个写句柄。打开时驱动与对端交换随机 64 位会话标识；握手完成后，每次 `pwrite` 仅发布一份快照。通过偏移 0 写完整的 `SRNOS_IpcRequest`。TTL 取 1～5000 ms，以对端的毫秒时钟标记写入时间。过期命令即使第一次被读到也会被丢弃，反复读取同一份命令不会延长寿命。实时核离线时返回 `ENODEV`；重新加载后，旧写句柄返回 `EPIPE`，应用需关闭再打开。

## 内存与运行边界

| 链路 | 保留窗口 | 布局 |
|---|---|---|
| A35 ↔ M33 | DDR `0x81201000`，1 KiB | 位于已有 ipc-shmem-1 carveout，之前的 4 KiB 供 resource table 和 trace |
| A35 ↔ M0+ | LPSRAM `0x200c3c00`，1 KiB | 固件 16 KiB 保留区的末尾，代码限 15 KiB；独立的 4 KiB 数据和栈区保持完整 |

两份链接脚本均生成由 remoteproc 装载时清零的独立 ELF 段，当前 ABI 占 832 字节。如果更换 BSP 或启用 M 核数据缓存，应复核共享窗口的内存属性。本目标的 M 核启动代码没有启用数据缓存。Linux 模块沿用 STM32 remoteproc 的 `ioremap_wc` 映射，使用有序的 MMIO 读写和屏障。

M33 在 CAN 工作线程创建后启动一个高于普通优先级、栈为 3072 字节的周期任务。CAN 收发工作线程保持原优先级。M0+ 在解码到完整帧的 UART 中断中发布上行，主循环每毫秒检查下行。空闲状态、停机应答和固件重载仍由各核原实现负责。

## M33 应用闭环接入

应用可在 `SRNOS_MP257MotorObservation.speed_trip` 中保留最近一次超速触发样本。`mp257-ipc motor` 在 `sources` 非零时返回 `last_speed_trip`。位 0 表示电机反馈转速触发，位 1 表示编码器滤波速度触发，两位可同时置位。记录包含 M33 触发时刻（ms）、反馈转速（rpm）、滤波速度（毫度/s）、相对参考位置（毫度）及触发前最后一次成功提交的原始输出。当前输出仍由顶层 `output_raw` 表示。旧版固件缺省此记录。修改回调结构后须重新构建相应 M33 应用及 SDK。

父项目可以实现公开的 `SRNOS_MP257MotorStep` 回调，在 IPC 的 1 ms 任务中读目标并返回观测。结构体及单位见 [MotorExchange.h](../../../Include/SirinEmbedOS/IO/MP257/MotorExchange.h)。`now_ms` 为本核时钟，`session` 随写者握手更新，`continuity` 随握手或命令过期更新。回调中的输入和输出缓冲仅在本次调用有效。每个板级控制应用自行定义有效槽位、角度参考、控制参数、反馈失效策略和输出限幅。

A35 的 Python 接口 `send_angles(fd, targets, mask, ttl_ms, drive=False)` 发布 16 个有符号毫度槽。预览和执行使用不同的操作码，TTL 范围均为 1～100 ms。应用返回非零 profile 后驱动才接受这两种操作。执行请求仍须满足应用的使能条件。使用 `clear_targets(fd)` 请求回到空闲。原 `mp257-ipc target` 和 `send_targets` 始终向控制回调提供 IDLE。应用上报的测量角度、参考、反馈年龄、状态和输出可用 `mp257-ipc motor` 读取。物理停机先结束写入并确认输出已释放，再停止 remoteproc。
