# 只读遥测示例

把 `app_main.cpp` 作为父项目应用源文件接入 SDK。全局 `Value<uint32_t>` 在调度前登记目录；应用启动后创建 1 ms 托管周期任务，单轮仅递增并发布计数，实际时间来自 `SRNOS_TimeUs()`。父项目无需 FreeRTOS、CMSIS、HAL 头。

变量允许主机以最高1000 Hz订阅，但订阅不会改变业务任务节奏。例子展示缓存接口，不承诺USB无损传送每个计数；数据序号与时间用于识别更新和丢失。主机SOURCE V1/V2查询、订阅与元数据解释见 [协议](../../Docs/HostLink.md#只读-source-v2)。本例不连接执行器、不提供控制保护。

运行期管理任务时保存创建句柄并检查 `ThreadCreate` 结果，通过 `ThreadDelete` 协作停止/回收；本示例任务作为应用生命周期常驻。实际生产项目须处理创建失败。构建与实际运行结果分别记录在 [CHANGELOG](CHANGELOG.md)。

## 父项目装配示例

在父项目已经 `add_subdirectory(SirinEmbedOS)` 后添加：

```cmake
set(SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/SirinEmbedOS")
sirinos_add_firmware(TelemetryExample
  "${SDK_DIR}/Examples/Telemetry/app_main.cpp")
```

同一固件只保留一份 `SRNOS_ApplicationMain()`，配置回调按项目集中维护。历史变更与验证见 [CHANGELOG](CHANGELOG.md)。
