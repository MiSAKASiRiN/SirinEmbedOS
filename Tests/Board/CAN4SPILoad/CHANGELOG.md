# Changelog

## Unreleased

- 2026-10-06：增加十秒CPU窗口、生产周期、CAN4唤醒/轮次、任务CPU与SPI每帧事务统计；IMU间隔保护线改为1.2ms。Classic正式档3×60s及1×600s，Quick档用于定位；诊断经CAN4 owner执行。 Classic目标3×60s＋600s通过，最高十秒CPU53.9632%，IMU保护线通过；FD格式/负载/恢复、资源及原Flash恢复见 [最终记录](../../../Docs/Validation/CAN4-Optimization-20261006/README.md)。

- 2026-10-06：补充首帧失败诊断、仅功能的快速对照以及CAN1仲裁采样点80%的测试专用wrap；生产位时序不变。逐窗判定包括实际生产周期，避免生产者被抢占后以较低吞吐误报压力通过。

- 2026-10-06：新增CAN1/CAN4四模式、单向/双向阶梯板测，采集SPI事务排队/耗时与IMU连续性；生产代码和资源策略不变，结果以独立板测记录为准。

## 2026-10-17 文档整理

- 使用说明保留当前合同、入口和示例；日期战报与修改记录归入本文件。仅调整 Markdown，代码、原始测量数据和第三方源码未改动。

### README.md 中保留的历史说明

以下内容描述原文所述时期，不作为当前用法；当前入口见 [README.md](README.md)。

Classic正式镜像的11个窗口为空载5s、两个方向单向3k各10s、双向各1k/2k各10s、双向各3k三轮60s、过载各6k 10s、恢复各1k 10s、最后双向各3k 600s。`ClassicQuick` 将目标档改为10s，仅用于定位。FD包含全部合法DLC与STD/EXT、单向/双向阶梯、过载恢复；8M64B各3k维持60s以表征上限，不将该档作为本轮硬验收。

```sh
cmake -S Tests/Board/CAN4SPILoad -B build/can4-spi-board-20261006/firmware -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$PWD/CMake/starm-clang.cmake" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/can4-spi-board-20261006/firmware -j8
```

`FD8MProbe` 跳过INS预热，只跑64个格式用例，不用于IMU或负载验收。本轮生产时序已对齐，旧Aligned专用wrap目标不再参与构建；旧证据仍保留。结果主结构维持26816B；IMU超阈计数现用1200µs，旧归档的1500µs口径不可混用。

2026-10-06优化前已完成44个窗口、379秒测量，结果包含未达标档和8M时序问题，见 [完整报告](../../../Docs/Validation/CAN4-SPI-20261006/README.md)。`summarize.py <含摘要的目录>`生成负载表、总摘要与IMU/SPI观测汇总；逐帧一致性检查通过不代表每档吞吐通过。

## 2026-10-06 优化后验证

Classic 1M/8B双向各3k完成3×60s＋600s，零丢帧/拒绝/TTL过期/硬件RX溢出，78个十秒CPU窗口最高53.9632%。双IMU/INS保持1kHz，含6k过载的完成间隔/采集/SPI排队峰值1143/151/112µs；FD五速率320格式用例和过载恢复通过，64B各3k在5/8M下CPU约61%。实测仅CAN1/CAN4短接，CAN2/3/UART同时满载与物理故障尚未覆盖；原Flash恢复并释放探针。详见 [最终验证与原始证据](../../../Docs/Validation/CAN4-Optimization-20261006/README.md)。
