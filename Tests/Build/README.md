# SDK 父项目构建验证

父项目只链接 `SirinEmbedOS::Platform`，应用源码只包含 `SirinEmbedOS/...` 公共头。平台传递 SDK 公共 include、统一配置宏、固件对象和必要链接选项；FreeRTOS、CMSIS、HAL、板级及服务实现的 include 路径只供 SDK 内部对象使用。

默认容量和配置覆盖入口为 `SirinEmbedOS/Config.h`。父项目通过 `SIRINEMBEDOS_CONFIG_HEADER` 提供自己的配置，不能依赖 SDK 的 `Config/FreeRTOSConfig.h` 或修改原生 RTOS 配置。

## 执行

将 ST Arm Clang、CMake、Ninja 放入 PATH 后运行：

```sh
python3 Tests/Build/check_sdk.py
```

只检查公共头、汇总编译和传递依赖错误而不构建固件时，运行 `python3 Tests/Build/check_sdk.py --public-headers-only`。

只检查公开命名时，运行 `python3 Tests/Build/check_public_names.py`。检查使用编译器 AST 识别声明：C 函数、类型和枚举采用 `SRNOS_`，C++ 全局命名空间采用 `SRNOS`，公有宏采用 `SRNOS_`；固定链接段边界符号和编译器隐式声明除外。

活跃自维护命名检查使用 `python3 Tests/Build/check_runtime_names.py --compile-commands build/Debug/compile_commands.json`，先完成该目录的构建。它扫描全部 SDK 实现、板级支撑、配置及公共头的旧文件名、旧标识符、宏和非作用域枚举；再根据当前编译数据库的准确对象输出读取全局定义，拒绝非 `SRNOS_` / `SRNOS::` 的跨文件符号。缺失、源文件更新后的旧对象或未装配源码会失败，不递归扫描 build 中历史对象。`--source-only` 可单独检查源码，但不代表已验证链接符号。

例外只列四个 Microchip 原文件，以及按所属文件逐项列出的 HAL、USB LL、FreeRTOS、Picolibc 和 SPI 适配 ABI 入口；整个自维护目录没有豁免。`runtime_names_abi.json` 逐项列出受外部合同约束的配置/trace 宏及原因，新增名称必须经过复核。CubeMX/第三方编译输入单独分类，构建时生成的两份 USB middleware 副本还须与已记录的补丁逐字相符。旧线协议 magic 和注释中的来源说明不作为代码命名错误。

脚本在临时目录构建父项目，不修改正式 Debug/Release 产物，也不连接硬件。检查覆盖：

- 每个公开 `.h` 独立以 C11 和 C++17 编译，`.hpp` 独立以 C++17 编译，再聚合编译所有公共头。C++专用接口使用 `.hpp`。
- 从编译器依赖清单核对完整 include 闭包，SDK 内依赖只允许进入 `Include/`；不靠在公共头前预先包含其他头来补齐依赖。
- 检查真实父项目和业务示例的编译命令，只允许 SDK 的 `Include/` 路径，不传递内部头或 HAL 配置定义。
- 父项目用 `__has_include` 验证 FreeRTOS、CMSIS-RTOS、HAL、CMSIS-DSP 头确实不在有效搜索路径中，避免只检查命令文本而遗漏隐式依赖。
- 构建辅助函数和手动链接两条接入路径；检查配置覆盖、C++/TLS、唯一中断入口、heap、D3 缓冲区和链接越界拒绝。
- 保留第三方哈希校验；这些检查只证明主机/编译链接合同，不代表板端时序和外设验收。

最终 ELF 基础检查还要求 `SRNOS_FDCANOnRxISR` 和 `SRNOS_FDCANOnTxEvent` 为 router 的强符号。共享头上的 weak 会使实现也继承弱属性，链接到空回调却仍构建成功；此门禁基于 CAN1/CAN3 实板复现，旧 ELF 必须被拒绝。单独编译 router 的主机替身不能覆盖这一跨翻译单元链接问题。

`EEPROMBringUp/kernel_test_adapter.c` 是显式内核测试，单独编译成 SDK 私有 OBJECT 目标，再将对象交给示例固件；它不能使示例业务源码重新获得内部头路径。此适配不是父项目内存分配 API。

`check_structure.py --compile-commands <当前构建目录>/compile_commands.json` 另外核对自维护源码装配和目录所有权；应传入本次新生成的编译数据库。

内存检查现要求 CAN Router 上下文位于 ITCM BSS、Watch 位于 ITCM，代码/BSS/Watch 不重叠且总长不超过 64 KiB；ITCM 初始化边界及 Flash 源对齐，DMA/BDMA/Fault 仍在各自允许银行。Core 主机测试验证 64 位清零/复制边界、空区间及复位再初始化，实际访存仍需板测。
