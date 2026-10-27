# Changelog

## 2026-09-25 快照导入

以下保留导入时的来源、编译选择和验证边界；当前配置见 [README](README.md)。

- 内核版本：**10.6.2**，以 `Source/include/task.h` 为准。旧工程 `FreeRTOSConfig.h` 开头的 10.3.1 注释不是实际源码版本。
- 导入日期：2026-09-25。
- 直接来源：`h723vg-v2-prototype/Middlewares/Third_Party/FreeRTOS/Source/` 的本地文件快照。
- 参考仓库 HEAD：`4ae5428c6fb9447b12a79a5cc30a8f47334f8f11`。HEAD 用于定位参考工程，导入内容以 `SHA256SUMS` 为准，不假定该本地快照等同于原版上游 tag。
- `CMSIS/Include/cmsis_os2.h` 和 `CMSIS/LICENSE.txt` 来自同工程 `Drivers/CMSIS/RTOS2/Include/` 与 `Drivers/CMSIS/`。
- 上游项目：https://github.com/FreeRTOS/FreeRTOS-Kernel ，CMSIS-RTOS2：https://github.com/ARM-software/CMSIS_5 。本次没有升级、下载替代版本或建立嵌套子模块。

内核保留 `Source/LICENSE`（MIT）；CMSIS wrapper 的 Apache-2.0 版权声明保留在源码中，完整许可证见 `CMSIS/LICENSE.txt`。**本次导入不修改任何第三方源码**，所有导入文件记录在 `SHA256SUMS`。

编译选择 tasks、queue、list、timers、event_groups、stream_buffer、heap_5、CMSIS-RTOS2 和 prototype 使用的 `GCC/ARM_CM4F` 浮点移植层。该移植层保留了对 Cortex-M7 r0p0/r0p1 的断言；不因为目录名有 GCC/CM4F 就改成其他版本的 port。当前 Cortex-M7 hard-float / ST Arm Clang 编译链接已验证，上板调度与 FPU 上下文切换仍待验证。
