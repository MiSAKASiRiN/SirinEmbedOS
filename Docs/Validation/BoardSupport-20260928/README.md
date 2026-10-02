# Board/Support 整理验证（2026-09-28）

本轮只执行静态、主机与固件构建验证。未运行 CubeMX Generate，未连接/复位/烧录 MCU，未操作调试器。工作区已有 I2C/CubeMX 改动作为基线保留；历史验证不被本记录覆盖。

## 改动与保留

原 Glue 的 25 个文件均通过 [重定位表](../../Migration/board-support-relocations.json) 对应到现有实现。Support 分为 System/IO/Devices/USB；指示器策略进入服务，USB 准备与 MCP 默认硬件绑定收拢；三个指示设备公共头共 23 项硬件定义移到私有头，值与原文件逐项一致。当前职责和 35 个 Source 文件的依赖决定见 [边界审查](../../BoardSupport.md)。

实施前在 `build/support-refactor-20260928/` 保存 `before/`、`before.patch`、`before-hashes.json` 和 `status-before.txt`。该目录是本地验证产物，不编入或提交 SDK。相对于此快照：

- IOC、HAL/CMSIS、Board 生成 CMake、链接脚本、第三方、已有验证档案、来源快照、Examples、Config 等共 301 个受保护文件逐字节保持。
- Board/Core 全部文件剔除 USER CODE 后逐字节一致；仅 main/i2c 的 USER CODE 头文件路径因分类调整。生成文件原 CRLF 保留，差异检查使用 `git -c core.whitespace=cr-at-eol diff --check`。
- SPI/I2C 拆出的六个板级函数体与原工作区逐字节一致，包括本轮开始前的 I2C 时钟/滤波检查修改。
- EEPROMBringUp、公开 I2C 头、EEPROM 文档、IOC 和生成脚本等无关用户改动逐字节保持；合并修改的根文档/AGENTS 在原内容上补充，没有恢复到 Git HEAD。
- 原始来源 SHA、source-manifest、header-map、glue-references 及第三方 ADAPTATIONS 保持；当前 ledger 通过重定位表更新目标与哈希，SDK-SHA256SUMS 刷新。

## 主机与静态验证

| 检查 | 结果 |
|---|---|
| 实施前主机测试 | 原 20 组全部通过 |
| `python3 Tests/run_host_tests.py` | 当前 21 组全部通过；Indicators 测试迁入 Services，新增 MCP2518FD 套件 |
| 指示器行为 | 提示关闭、重复准备、黄色一次、三音各 80 ms、初始化失败、DMA busy、500 ms 翻转、延迟不补发通过；测试同时编入板级初始化与服务实现 |
| USB 启动 | 实际服务与板级准备配合 HAL/USBD 桩；非法 PCD、固定 GPIO 属性、四个启动失败点、DeInit、重试顺序及成功后重复调用通过 |
| MCP 配置 | 实际 can_board 与 drv_spi 配合 HAL/SPI 桩；默认配置、无效 ID、三路 handle、显式 Attach、覆盖默认、初始化复用通过 |
| `check_structure.py --compile-commands …/minimal/compile_commands.json` | 378 源/头/工具合同、28 个唯一 HAL callback 源码所有者、49 个来源模块、Source/Support 所有实现各编译一次通过 |
| `check_i2c_timing.py` | IOC、生成初始化及 Support 均为 `0x10C7182D`；Fast mode 时序合同通过 |
| 公开/私有边界 | 公共头不 include Support 私有头；父项目应用编译命令不含 Support include 路径；有效构建与源码不引用旧 Glue |

结构检查必须显式指定本次构建的 compile_commands，不再默认读取旧 Debug 缓存。旧路径只保留于历史导入/验证记录和重定位映射。

## 固件与链接验证

使用 ST Arm Clang 工具链，所有输出在独立目录，未覆盖已有 `build/Debug` 等调试产物：

```sh
cmake -S . -B build/support-refactor-20260928/minimal -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/support-refactor-20260928/minimal -j8
cmake -S Board -B build/support-refactor-20260928/board -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/support-refactor-20260928/board -j8
cmake -S . -B build/support-refactor-20260928/eeprom -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSIRINEMBEDOS_EXAMPLE=EEPROMBringUp
cmake --build build/support-refactor-20260928/eeprom -j8
python3 Tests/Build/check_sdk.py
python3 Tests/Build/check_structure.py --compile-commands build/support-refactor-20260928/minimal/compile_commands.json
python3 Tests/Core/check_foundation_elf.py build/support-refactor-20260928/minimal/SirinEmbedOS.elf
python3 Tests/Core/check_foundation_elf.py build/support-refactor-20260928/board/sdk/SirinEmbedOS.elf
python3 Tests/Core/check_foundation_elf.py build/support-refactor-20260928/eeprom/SirinEmbedOS.elf
```

三个独立入口及父项目九个目标（RobotFirmware、ManualFirmware、PrototypeBindings、ParameterExample、BringUpExample、SensorBringUpExample、INSBringUpExample、RawBinding、AllComponents）全部通过。父项目测试还验证第三方哈希、配置覆盖、C++/TLS、MSP/libc 容量及溢出拒绝。链接检查增加 SVC/PendSV/SysTick、DMA1 Stream0/1、TIM7 的唯一强符号和向量表目标校验，保留六类 Fault 入口和内存段检查。

| Debug ELF | text | data | bss |
|---|---:|---:|---:|
| 整理前 Minimal | 249188 | 980 | 424900 |
| 整理后 Minimal | 249184 | 980 | 424896 |
| Board 独立 Minimal | 249184 | 980 | 424896 |
| EEPROMBringUp | 253124 | 980 | 443264 |

Minimal 的 `.text` 减少 4 B，来自职责拆分后的代码生成/布局变化；`.rodata` 大小不变。全部 b/B/d/D 存储符号的名称、大小和类型多重集合一致，BSS 减少 4 B 为链接排列/填充变化，未删除运行状态。D1 heap/libc 起点随 32 B 对齐提前 32 B，容量不变；DTCM/D2/D3/ITCM、DMA/BDMA/Watch/retained 与 MSP 段地址及大小保持。没有修改链接脚本，不要求符号地址或 ELF 字节一致。

详细本地日志：`host-before.log`、`host-after.log`、`parent.log`、`build-before.log`、`build-after.log`、`board-build.log`、`eeprom-build.log`、`preservation.json`。

## 仍需板端验证

结构与主机检查不证明硬件正常。声光、USB 枚举/DTR/通信、真实 CAN、传感器和温控运行尚未在本轮固件上复测；本次没有作这些实机验收声明。
