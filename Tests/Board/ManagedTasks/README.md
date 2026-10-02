# 托管任务板端验证

这是独立的父项目测试固件，通过公开 Kernel API 驱动生命周期与同步检查。`kernel_adapter.c` 仅提供测量所需的原生 RTOS/DWT 适配；`adc_fault_adapter.c` 通过链接包装进行可控故障注入，不进入 SDK/生产固件。不会向 CAN 发送控制帧或写入 EEPROM。各对照构建均关闭 IMU 加热，开启板载传感器/INS，并运行 1 ms、约 20 µs 工作量的应用任务。

`app.cpp` 只链接公共 Platform 使用要求；两个原生适配源编入独立 OBJECT 目标，私有 include 不传播给应用。公共边界收敛后不能通过给整个测试固件添加 FreeRTOS/HAL 路径来绕过这一合同。

```sh
cmake -S Tests/Board/ManagedTasks -B build/ManagedBoard-On -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$PWD/CMake/starm-clang.cmake"
cmake --build build/ManagedBoard-On -j8
# 统计关闭对照：新目录，追加 -DBOARD_STATS=OFF；监测关闭追加 -DBOARD_MONITOR=OFF。
```

烧录需要用户明确授权，先检查探针/调试会话所有权并保存现场。该项目保留 Release 优化并追加 `-g3`，便于读取 `board_test`，不要与其他 ELF 混用。每个测量窗口内禁止暂停 CPU 或接入高流量遥测。5 秒稳定后记录 30 个一秒窗口；必须等待 `board_test.stage == 3`、`samples == 30` 再读取。中途暂停或未完成的样本不得作为完整对照。

`board_test` 包含 400 次创建/Join/Delete、TLS/errno、初始化失败、停止唤醒、通知、删除超时、同步对象忙、池耗尽及回收的检查计数。CPU 使用 Idle 累计运行周期差与实际 DWT 时间差计算；包含 RTOS 的中断归属。hook 基准逐次屏蔽中断测量完整 Begin/End 调用，128 次之间阻塞一 tick，只减去计数器读取成本。它测量无抢占基准，不代表冷缓存或所有负载的绝对最坏时间。

完成基准后可在调试器设置 `board_adc_fault`，每次只消耗一个命令：1=完成报告错误，2=丢失完成，3=完成后延迟处理 20 ms，4=无法确认停止。真实 ADC 采集/停止仍执行；4 模拟确认失败，硬件实际停止，并不制造物理 DMA 卡死。通过 `board_adc_injected`、监测快照和遥测核对失效/恢复/旧值/时间戳；命令 4 后应停用 ADC、继续任务统计，复位恢复。

`cdc_exercise.py --port <CDC1> --output <新目录>` 订阅三条监测流和五条传感器流，正常读取、停读 12 秒、恢复读取、拉低 DTR 5 秒并重连。只修改遥测订阅，不改变采样频率。输出 JSONL 和完成元数据；必须同时检查板端 `SRNOS_DebugHostLink.qos` 的 coalesced/drop_old 才能确认实际发生背压。该测试不是物理 USB 拔插。

具体板端结果和未测边界见 `Docs/Validation/ManagedTasksBoard-20260928/README.md`。仪表精度对照、外部 CAN 持续负载、真实 ADC 硬件故障及长时间压力需要额外实验。
