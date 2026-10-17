# FreeRTOS 来源、配置与使用边界

内核版本为 **10.6.2**，以 `Source/include/task.h` 为准。直接来源是参考工程 `h723vg-v2-prototype/Middlewares/Third_Party/FreeRTOS/Source/` 的本地快照，参考 HEAD 为 `4ae5428c6fb9447b12a79a5cc30a8f47334f8f11`。导入内容以 [SHA256SUMS](SHA256SUMS) 为准；参考 HEAD 不表示本地快照与上游 tag 逐字相同。

上游：[FreeRTOS-Kernel](https://github.com/FreeRTOS/FreeRTOS-Kernel)、[CMSIS_5](https://github.com/ARM-software/CMSIS_5)。`CMSIS/Include/cmsis_os2.h` 与 `CMSIS/LICENSE.txt` 来自参考工程 `Drivers/CMSIS/`。内核保留 [MIT 许可证](Source/LICENSE)，CMSIS wrapper 保留源码版权声明与 [Apache-2.0 许可证](CMSIS/LICENSE.txt)。

## SDK 接入

配置位于 [Config/FreeRTOSConfig.h](../../Config/FreeRTOSConfig.h)，生命周期/hooks 位于 [Source/Kernel](../../Source/Kernel/README.md)，heap_5 区域与启动位于 [Board/Support/System](../../Board/Support/README.md)，内存布局由 Board/Linker 管理。应用使用公开 Kernel/Thread/Sync API，不直接包含第三方私有头。

组件选择 tasks、queue、list、timers、event_groups、stream_buffer、heap_5、CMSIS-RTOS2，以及参考工程的 `GCC/ARM_CM4F` 浮点移植层。该移植层的 Cortex-M7 r0p0/r0p1 断言保留；不能只根据目录名替换 port。`croutine.c`、MPU wrapper 等随快照保存，但不参与当前构建。

## 完整性检查示例

从 SDK 根目录运行：

```sh
(cd ThirdParty/FreeRTOS && shasum -a 256 -c SHA256SUMS)
```

哈希通过只证明快照完整，不证明调度/FPU 上下文或板端时序。项目适配优先放 Kernel/Board；确需上游补丁时同步来源、哈希和 [CHANGELOG](CHANGELOG.md)，不删除上游版权。
