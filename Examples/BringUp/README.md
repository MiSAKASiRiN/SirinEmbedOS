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
| `heap` | 公开 KernelResources 的 heap_5 空闲/历史最低空闲，以及父项目池容量、当前/峰值与分配失败；不包含 libc arena 或 MSP。 |
| `tasks` | 缓存的任务名、优先级、状态；栈水位为 UINT32_MAX（未测量），不直接扫栈。 |
| `task_snapshot_complete` | 有界逐槽缓存读取是否完成；各槽实际时间不同，不代表同一瞬间的系统。 |
| `imu`、`internal_imu_age_ms` | 正式 INS 一致快照和按 DWT 时基计算的姿态年龄；无姿态时年龄为 `UINT32_MAX`。 |
| `internal_imu_recent` | 正式 INS 的 VALID 标记及本例观测年龄阈值；仍须看 INS 状态/epoch，不代表已经标定或完成精度验收。 |
| `logging` | 日志队列/丢弃、USB 配置和流量等累计计数；结合 CDC0 实际输出判断链路。 |

USB 没有枚举或 CDC0 没打开时，仍可通过调试器看快照。CDC1 保持默认 HostLink；本例未声明业务参数，并关闭启动业务参数恢复，因此 `parameter_load_status=OK` 不表示曾成功读取 EEPROM。

示例本身只观测，未添加电机命令、CAN 测试帧、UART 绑定或 EEPROM 写入。SDK 默认固定服务仍会初始化 CAN、共享 SPI、IMU、指示器等；本例通过冻结的 `sensors.heater_enabled=false` 禁止加热（包括恢复路径）；使用正式 INS，本例关闭业务参数启动加载；Storage 与 HostLink 存储命令保留默认启用，启动仍读取身份和 IMU 校准。旧融合/校准入口已替换为当前 Storage/IMUCalibration，主机显式触发保存或校准会写 EEPROM；示例自身不自动写入。原始双 IMU 对比请选择 SensorBringUp，完整姿态验证请选择 INSBringUp。

本例启用 `bringup_indicators=true`：调度前第一灯黄色提交一次、蜂鸣器播放 1/1.5/2 kHz 各 80 ms；调度后原 Indicators 线程每 0.5 秒切换第一灯明灭，完整心跳周期 1 秒，不等待 CAN 或 USB。后续 BringUp 示例沿用这一配置。

示例通过公开 SystemMonitor 缓存读取任务，不再暂停调度遍历任务或扫描栈。资源查询和输出仍有开销；实时性需结合正常业务和统计开关对照。

板端验收顺序见 [后续开发清单](../../Docs/Development.md)。构建通过与板端实测结果分别记录。

## 托管接口迁移

长期应用工作改为托管 step 描述，周期等待由 SDK 负责；观察 srnos_example_create_result 检查创建结果。 旧 ThreadConfig.entry 不再支持；公共创建/停止/删除及同步用法见 Source/Kernel/README.md。本次编译不意味着重新执行板端测试。
