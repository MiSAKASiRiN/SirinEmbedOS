# 首次上板观测应用

每 5 秒采样一次，通过默认 USB CDC0 日志输出摘要，并将完整结果保存在调试符号 `srnos_bringup` 中。它没有自动判定整板 PASS 的逻辑。

```sh
export PATH="/opt/ST/STM32CubeCLT_1.22.0/st-arm-clang/bin:$PATH"
cmake -S . -B build/BringUp -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DSIRINEMBEDOS_EXAMPLE=BringUp
cmake --build build/BringUp -j 8
```

产物为 `build/BringUp/SirinEmbedOS.elf`；CLion 可添加独立 CMake Profile，构建目录和选项使用上述值。默认根 Debug/Release 仍选择 Minimal。父项目可以直接将此目录的 `app_main.c` 传入 `sirinos_add_firmware()`，不要同时编译另一个应用入口。

## 观测方法

暂停 MCU 后检查 `srnos_bringup.sampling == false`，此时是完整的一次发布；若为 true，继续执行至该次采样结束。这个符号仅供停机调试，不提供运行期多线程读取合同。各项采样发生在不同时刻，不能视为整板原子快照。

| 字段 | 含义与限制 |
|---|---|
| `samples`、`kernel_tick`、`hal_tick_ms` | 应用线程心跳与两套时基；持续运行后应递增。HAL tick 和 Kernel tick 的起点不同。 |
| `system`、`configuration_frozen` | 配置冻结、启动阶段、原生 CAN 初始化、USB 启动结果。初始化成功不证明总线收发或主机枚举。 |
| `startup_heap_budget_bytes` | 配置校验采用的保守启动预算，不是实测已用内存。 |
| `heap` | heap_5 当前空闲、历史最低空闲、最大连续空闲块及分配统计；不包含 libc arena 或 MSP。 |
| `tasks` | 任务名、FreeRTOS 优先级、状态和历史最小空闲栈字节数。不是当前 SP 余量，也不能证明极端路径不会溢出。 |
| `task_snapshot_complete` | 64 个有界槽位是否容纳完整任务列表；失败时不解释为空任务系统。 |
| `imu`、`internal_imu_age_ms` | 原公共 IMU 快照和按 HAL 时基计算的数据年龄；无快照时年龄为 `UINT32_MAX`。 |
| `internal_imu_recent` | 内部 IMU online、valid 且数据年龄不超过 100 ms；仅为本例观测阈值，不代表校准、方向或融合正确。 |
| `logging` | 日志队列/丢弃、USB 配置和流量等累计计数；结合 CDC0 实际输出判断链路。 |

USB 没有枚举或 CDC0 没打开时，仍可通过调试器看快照。CDC1 保持默认 HostLink；本例未声明业务参数，并关闭启动业务参数恢复，因此 `parameter_load_status=OK` 不表示曾成功读取 EEPROM。

示例本身只观测，未添加电机命令、CAN 测试帧、UART 绑定或 EEPROM 写入。SDK 默认固定服务仍会初始化 CAN、共享 SPI、IMU、指示器等；IMU 温控和原有校准/持久化行为仍保留，不能将此固件理解为整板外设静默模式。

任务栈扫描会短暂挂起调度器，并带来测量扰动。本例用于低频资源观察，不用于证明实时性；正式压力测试应使用正常应用和已有计数器，关闭这类全任务栈扫描。它也不能覆盖应用入口到达前的故障，此时仍需断点、Fault retained 记录和 Debug Watch。

板端验收顺序见 [后续开发清单](../../Docs/Development.md)。构建通过与板端实测结果分别记录。
