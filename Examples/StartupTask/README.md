# 独立文件注册固定周期任务

[heartbeat_task.c](heartbeat_task.c) 是一个可加入父项目的 C11 源文件。它保存心跳计数、实现单轮 step，并通过 `AppRegisterHeartbeatTask(config)` 将描述追加到 SDK 的启动任务列表。[app_main.cpp](app_main.cpp) 是配套的 C++17 入口，在配置回调中检查注册结果，在应用入口向默认的 USB CDC0 日志后端提交一条启动消息。

在父项目的 `add_subdirectory(SirinEmbedOS)` 之后添加以下固件目标，可直接使用仓库中的两份源文件：

```cmake
sirinos_add_firmware(StartupTask
    SirinEmbedOS/Examples/StartupTask/app_main.cpp
    SirinEmbedOS/Examples/StartupTask/heartbeat_task.c)
```

在已有固件中，只需把任务文件加入目标源清单，让现有的配置回调调用注册函数一次。C++ 入口使用例中的 `extern "C"` 声明连接 C 实现。运行时可在调试器观察 `heartbeat_count` 每秒递增；任务由 SDK 排期，`SRNOS_ApplicationMain()` 返回后继续执行。配置沿用平台的基础采样，示例关闭融合和加热。

启动列表适合与固件同寿命的任务。名称和参数需要覆盖任务使用期，不能引用注册函数的局部变量。需要持有句柄以便停止、等待或删除任务时，在应用线程内运行 `SRNOS_ThreadCreate()`。完整父项目和运行期创建方式分别见 [QuickStart](../../QuickStart.md#创建父项目) 和 [Minimal](../Minimal/README.md)。
