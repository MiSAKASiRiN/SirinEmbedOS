# Changelog

## Unreleased

- 2026-10-06：六镜像实板执行与固定ARM短枚举布局解码完成；54窗口计数核对、356功能帧及两个60秒目标负载通过，过载拒绝/TTL与接纳后交付差额一致。新增离线summary复查，记录动态栈/IMU与身份记录/LIS限制，完整Flash恢复后CDC监测通过。见 [证据](../../../Docs/Validation/CANFD-Board-20261006/README.md)。

- 2026-10-05：新增六种初始化模式的CAN1/CAN3板测，全部合法DLC/标准扩展ID/双向全payload校验，1ms生产者阶梯及60秒目标负载。记录硬件、router、CPU、INS/传感器和窗口外栈水位；实际板测结果另归档，不以编译代表通信通过。

## 2026-10-17 文档整理

- 使用说明保留当前合同、入口和示例；日期战报与修改记录归入本文件。仅调整 Markdown，代码、原始测量数据和第三方源码未改动。

### README.md 中保留的历史说明

以下内容描述原文所述时期，不作为当前用法；当前入口见 [README.md](README.md)。

2026-10-06 已完成六模式实板执行，Classic8B/FD8M64B各3k收发60秒通过；逐档结果、过载与恢复证据见 [实板记录](../../../Docs/Validation/CANFD-Board-20261006/README.md)。

```sh
cmake -S Tests/Board/CANFDLoad -B build/canfd-board-20261005/firmware -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$PWD/CMake/starm-clang.cmake" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/canfd-board-20261005/firmware -j8
```

```gdb
p SRNOS_TestCAN.phase
p SRNOS_TestCAN.failure
p sizeof(SRNOS_TestCAN)
dump binary memory build/canfd-board-20261005/classic.bin &SRNOS_TestCAN ((char*)&SRNOS_TestCAN+sizeof(SRNOS_TestCAN))
```

主机运行 `python3 Tests/Board/CANFDLoad/decode.py <结果.bin>` 或 `python3 Tests/Board/CANFDLoad/summarize.py <结果目录>`。decoder固定使用本版ARM结构布局；本次通过GDB验证总长11880 B、阶段1176 B、hardware80 B，stages偏移104、stacks偏移10692。公共统计结构变化后必须重新核对这些布局，不能只靠输出数值看似合理。`summarize.py` 将过载未交付与软件TTL核对，要求零无法解释的丢帧和IMU周期损失；逐档是否达到请求负载另行标记，不将预期背压当作通信损坏。
