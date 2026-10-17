# CAN1 / CAN3 Classic 与 FD 板测

在已确认 CAN1、CAN3 的 H/L 对应短接、且授权板测的环境使用。CAN2/CAN4 不发送测试数据，结果不能外推到这些通道。六个独立镜像在初始化前选择模式：Classic1M、FD1M无BRS、FD仲裁1M且数据2/4/5/8M。模式不会运行期改变；切换镜像必须复位。

应用只使用公共头，`adapter.c` 独立获得私有硬件诊断、Idle CPU及栈水位。正常IMU/INS/温控/监测服务保留；允许SDK默认启动读取校准和参数，不发SAVE、不写EEPROM。等待INS有效最多120秒，然后稳定3秒；超时结束并标记失败。

先做逐帧功能测试：CAN1→3和CAN3→1、标准和扩展ID、全部合法DLC，校验每字节与实际flags。Classic共36帧（0..8B），FD共64帧（0..8/12/16/20/24/32/48/64B）。一次只有一个功能帧在途，200ms未交付立即中止。

随后1ms托管生产者分批提交两个方向流量，记录实际窗口和生产者周期，按序号及全部payload逐字节验收。Classic依次空载、单向1k、双向各1/2/3/4/5k、各1k恢复；3k档持续60秒。FD包括8B各3k和64B各1/2/3/4k，8M/64B/各3k持续60秒，其他数据速率该档10秒。低位速率64B高档预计超过物理带宽，允许暴露排队/拒绝/TTL上限，但不能将实际提交低于请求或排空后的接收总数称为严格达标。

每阶段停止提交后排空500ms，软件TTL100ms。统计包含活动窗口实收、排空后实收、拒绝/缺序/乱序/数据错误、软件队列与硬件RX丢帧、TEC/REC/Bus-Off、CPU和INS/四传感器增量。ECR/PSR仅在阶段边界采样，可能清除硬件的累积/最近错误字段；不将它们当作完整错误历史。栈扫描仅在全部窗口结束、停止流量后执行，FreeRTOS此时会暂挂调度。

```sh
cmake -S Tests/Board/CANFDLoad -B build/CANFDLoad -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$PWD/CMake/starm-clang.cmake" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/CANFDLoad -j8
```

板测前保存1MiB原Flash和匹配ELF。每镜像下载后验证装载段，设置仅在 `SRNOS_TestCANDone` 命中的硬件断点，复位后连续运行至结束；阶段中不得暂停调试器。仅 `SRNOS_TestCAN.phase==3` 表示可导出完整结果；`failure==0` 表示测试流程正常结束，负载是否达标仍需检查接纳/实际速率/交付/错误及IMU数据。测试结束恢复原Flash并完整读回比对，再复位运行、释放本次会话。

此测试不覆盖CAN2/CAN4流量、共享SPI上的外置CAN压力、UART期限、拔线/Bus-Off故障注入或外部仪器位时序测量。

调试器在结束断点暂停后，例如导出Classic：

```gdb
p SRNOS_TestCAN.phase
p SRNOS_TestCAN.failure
p sizeof(SRNOS_TestCAN)
dump binary memory build/CANFDLoad/classic.bin &SRNOS_TestCAN ((char*)&SRNOS_TestCAN+sizeof(SRNOS_TestCAN))
```

主机运行 `python3 Tests/Board/CANFDLoad/decode.py <结果.bin>` 或 `python3 Tests/Board/CANFDLoad/summarize.py <结果目录>`。decoder固定使用本版ARM结构布局；使用准确的目标 ELF 在 GDB 中检查总长、阶段、hardware 及 stages/stacks 偏移，并与 decoder 的布局输出逐项比较。公共统计结构变化后必须重新核对这些布局，不能只靠输出数值看似合理。`summarize.py` 将过载未交付与软件TTL核对，要求零无法解释的丢帧和IMU周期损失；逐档是否达到请求负载另行标记，不将预期背压当作通信损坏。

历史镜像的 ABI 尺寸、负载测量与恢复结果见 [CHANGELOG](CHANGELOG.md)。解码脚本在输入旁写 JSON，复核历史结果前先把 `.bin` 复制到新的 `build/` 目录。
