# Minimal

通过公开 [Startup.h](../../Include/SirinEmbedOS/Kernel/Startup.h) / [Thread.h](../../Include/SirinEmbedOS/Kernel/Thread.h) 接入 SDK，根 CMake 默认构建此应用。实际代码见 [app_main.c](app_main.c)；完整生命周期说明见 [Kernel](../../Source/Kernel/README.md)。

## 父项目如何建立线程

本示例采用运行期创建：SDK 调度并完成应用启动屏障后调用 `SRNOS_ApplicationMain()`；该入口声明 Heartbeat 的 `SRNOS_ThreadConfig`，调用 `SRNOS_ThreadCreate()`，将句柄保存到 `application_task`，然后返回。Heartbeat 每 1000 ms 执行一次 `application_step`，更新 `sirinos_minimal_heartbeat`，等待与循环由 SDK 完成。创建后的任务不随 ApplicationMain 返回而结束。

| 字段/变量 | 本例含义 |
|---|---|
| `name="Heartbeat"` | 应用线程名，使用唯一的 1～15 字节名称，不使用 SDK 保留的 `SRNOS_` 前缀。 |
| `step=application_step` | 每次只执行一轮并返回 CONTINUE，不写永久循环或周期 Delay。 |
| `stack_bytes=4096` / `priority=24` | 栈单位为字节并包含 TLS；父项目优先级允许 1～39。 |
| `mode=SRNOS_TASK_PERIODIC` / `period_ms=1000` | 初始化成功后立即首轮，之后固定相位、错过周期不补跑。 |
| `srnos_example_create_result` | ApplicationMain 执行创建后记录返回值；0 表示创建成功，不表示整个系统所有设备健康。 |
| `application_task` | 调用者持有的生命周期句柄，供 GetInfo、Notify、RequestStop、Join、Delete 使用。 |

本例没有 init/cleanup/argument，因为业务仅更新静态计数。父项目需要一次性准备与资源释放时，分别提供 init 和 cleanup；业务参数使用静态对象或由管理者持有的存储，不能传入 ApplicationMain 返回后失效的局部对象。Create 会复制名称和描述，但不会复制参数指向的数据。

## 查询和回收

生产应用应检查 Create 返回值；资源不足时保留业务状态并由应用决定重试或降级。Create 成功后，用 `SRNOS_ThreadGetInfo()` 查询初始化结果和状态，不能把创建成功等同于 init 成功。

需要停止时，由持有句柄的另一个应用任务调用 `SRNOS_ThreadDelete(application_task, 100)`，或依次 RequestStop、Join、Delete。只有 Delete 返回 OK 后才清空句柄并释放参数；TIMEOUT/BUSY 时保留资源、稍后重试。自然结束/失败的线程也保留对象直到显式删除。回调内主动结束返回 STEP_STOP/FAILED，不自行调用 Delete。本例让心跳长期运行，因此没有自动回收流程。

需要随配置启动的固定线程时，可改用 `SRNOS_ApplicationConfigure()` 的 `threads[]/thread_count`，参考 [启动列表示例](../../Source/Kernel/README.md#启动配置中的固定任务)；这与当前运行期 Create 是两种选项，不要对同一任务同时使用。启动列表不会向应用返回创建句柄，且其中的任务不保证晚于 ApplicationMain 执行。

## 父项目移植与验证边界

只实现一份 ApplicationMain/ApplicationConfigure，不复制另一个 main 或再次启动调度器。旧 ThreadConfig.entry 已删除；一般业务代码通过 SDK 使用通知、Delay/DelayUntil 和同步对象，不再直接调用 FreeRTOS/CMSIS API，也无需统计 hook。默认 32 KiB 应用池由线程栈和同步对象共用，数量与容量限制见 Kernel 文档。

根 [接入示例](../../README.md#父项目接入) 说明 CMake 子模块接入；[板测记录](../../Docs/Validation/ManagedTasksBoard-20260928/README.md) 记录已执行的正常 Release 与专用测试固件验证。本轮仅补充文档，不重新烧录或改变示例运行行为。
