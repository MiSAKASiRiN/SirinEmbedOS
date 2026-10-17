# CAN1 / CAN3 外接回环吞吐测试

仅在用户确认 CAN1 与 CAN3 的 H/L 对应接通、没有受控执行器且授权板测后烧录。沿用板级 1 Mbit/s 经典 CAN，不改 IOC/HAL。固定测试 ID 0x601/0x603、8 字节负载包含阶段、来源、序号与异或校验，接收计数来自对侧物理控制器。默认 IMU/INS/加热/统计继续运行，不访问 EEPROM。

应用只用 SDK 公共头；`adapter.c` 独立目标提供原生诊断、ECR/待发送状态和 Idle CPU 观测，不向应用传播私有路径。ECR 只在阶段边界采集，不作为无副作用外部探针接口。

```sh
cmake -S Tests/Board/CANThroughput -B build/CANThroughput -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$PWD/CMake/starm-clang.cmake" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/CANThroughput -j8
```

先等待 INS 输出有效（上限 120 秒，超时停止），再稳定 5 秒；记录每段前后 INS phase/flags/publications，不能把预热期间的循环数当作完整融合负载。随后自动执行：空载 5 秒；两个方向各 1000 帧/秒、2 秒；双向各 1000/2000/3000/4000/5000 帧/秒、每档 10 秒；最后双向各 1000 帧/秒恢复 5 秒。生产者为托管 1 ms 周期线程，每轮按配置提交小批次，超期不补跑。双向速率相加占用同一物理 CAN 总线，不是全双工带宽；高档允许暴露容量上限，不预设全部通过。

每段结束停止提交并排空 500 ms（软件 TTL 100 ms）。记录实际窗口、提交接纳/拒绝、对侧接收、缺序/乱序/校验错误、软件过期、RX ring 丢弃、硬件错误与待发状态、CPU 和 INS/传感器增量。`tx_enqueued` 仅表示入硬件队列；`tx_complete_events` 是可发送通知，均不能替代对侧实际收到的帧数。软件接纳后 TTL 过期也要计入未交付。

仅在 `SRNOS_TestCAN.phase == 3` 时取完整结果；测试窗口内不要暂停调试器。无接收、bus-off 或排空后仍待发会中止后续阶段。固件阶段结束后停止发送，不循环重跑；测试后恢复原正常固件。该测试不涵盖 CAN2/CAN4、物理断线、bus-off 恢复、所有 ID/数据位填充组合或外部仪器线速测量。

实际负载与 ADC 测量结果见 [CHANGELOG](CHANGELOG.md) 及[证据阅读指南](../../../Docs/Validation/CAN13-ADC-20260928/README.md)。
