# Logging

父项目只包含 `SirinEmbedOS/Services/Logging/Log.h`，使用 `SRNOS_LogPrintf()`、`SRNOS_LogVPrintf()` 或 `SRNOS_LOGI()` 等宏，无需包含 FreeRTOS、CMSIS 或 HAL。等级、返回值、统计结构均为 SDK 自有类型；日志调用不接受 RTOS timeout 参数。

```c
#include "SirinEmbedOS/Services/Logging/Log.h"

void report_voltage(float voltage)
{
    SRNOS_LogResult result = SRNOS_LOGI_SRC("Power", "input %.2f V", (double)voltage);
    if (result != SRNOS_LOG_OK) {
        /* 按业务需要累计未接收计数；不要递归记录日志。 */
    }
}
```

## 调用与资源合同

`Printf/VPrintf` 限任务或启动上下文，ISR 调用返回 `SRNOS_LOG_ISR_FORBIDDEN`。格式化正文在调用者上下文完成，正文最多 255 字节并以 NUL 结尾；超长内容截断并计数。来源名、正文及时间戳复制到日志记录中，函数返回后不再持有调用者字符串。

调度运行后使用固定容量消息池和队列。默认 64 个消息块、20 个排队记录；申请和入队都是零等待。池不足时最多取出并释放一条旧记录后重试一次；队列满时同样最多丢弃一条旧记录后重试一次。并发情况下仍可能失败，函数返回失败并释放自己持有的消息块，不等待后台线程腾出空间。成功表示新记录已入队，不代表 USB 或 UART 已发送成功。

启动预算按默认消息池载荷、队列载荷/控制块、2048 B worker 栈、TCB 及 1024 B 的池管理/TLS/分配余量计算，并保留 16 KiB 下限；预算与创建使用相同容量常量。当前 ARM 配置中，消息块为 296 B，64 块载荷共 18944 B；加上线程栈 2048 B、TCB 112 B、队列载荷 80 B、队列控制块 80 B 和余量后，总预算为 **22288 B**。这些是编译常量，不能将旧 16 KiB 预留视为完整日志需求或将预算当作板端实测用量。模块预算由 `SRNOS_SystemHeapBudget` 汇总，静态 USB 缓冲另见 map。

| 结果 | 含义 |
|---|---|
| `SRNOS_LOG_OK` | 已入队；可能替换了旧记录 |
| `SRNOS_LOG_FULL` | 队列未装配或有界重试后仍满；启动阶段只捕获到启动 RAM 缓冲时也返回此值 |
| `SRNOS_LOG_NO_MEMORY` | 消息池有界重试后仍无可用块 |
| `SRNOS_LOG_INVALID` | 空格式串或无效等级 |
| `SRNOS_LOG_ISR_FORBIDDEN` | 从 ISR 调用 |
| `SRNOS_LOG_ERROR_RESULT` | 底层队列返回其他错误 |

`SRNOS_LOG_TIMEOUT` 是结果类型中的保留值；当前零等待日志接口不会返回它。调度前的日志仅写入已有的启动 RAM 捕获缓冲，待 USB 启动回放；调度运行或锁定调度时，未装配、资源不足及拥塞均不会转为同步输出，也不会重新进入启动回放。统计中的 `direct_printed` 保留布局，目前恒为零。

## 后端与线程

UART 后端需要已配置且启用 TX 的独占 Serial 通道；系统预检与绑定共用校验。只有选择 UART 日志后端时才使用 `log_port`，USB/SWO 后端不会因残留的 `log_port` 配置而绑定串口。

在系统启动配置中选择 `log_backends`；UART 后端还须由 Serial 配置明确分配日志用途。默认 USB CDC，SWO 默认关闭。编译期开关、所选后端和各后端等级 mask 共同决定输出范围；正常日志入队不检查物理链路是否就绪。后端绑定和选择在启动阶段冻结，冻结后的 `SRNOS_LogSetBackendMask()` 不改变配置。不默认占用 USART1，不把协议端口当备用输出口。

`SRNOS_Log` 使用 2 KiB 栈和 Low 优先级，通过 SDK 私有统一线程入口创建。队列的 10 ms 等待位于循环统计之外；整行格式化、投递与 USB 维护在单轮处理内。公开日志调用只入队，后台线程经已配置的非阻塞 Serial/USB sink 输出；后端拥塞可丢弃并计数。调度、资源装配、transport、输出函数和任务入口均保留在私有头内。

现有 libc `__io_putchar()` 仍由 SDK 适配到已配置的非阻塞输出 sink，并不经过上述结构化日志队列；它不是资源耗尽的回退路径。Fault 紧急输出保持独立，不依赖本服务。`SRNOS_LogGetStats()` 获取入队、丢弃、截断、USB 和 UART 计数；Debug Watch 对象名为 `SRNOS_DebugLog`。

主机测试不证明板端输出吞吐、线程调度开销或实机 CDC 背压时序。
