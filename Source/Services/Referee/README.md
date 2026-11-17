# 裁判系统与 UI

公开入口为 `SirinEmbedOS/Services/Referee/referee.h`；`UI/` 提供编码，`Transmitter/RefereeTransmitter.h` 提供发送。父项目在启动配置声明 Serial 通道和 `referee_port`，使用 [PrototypeBindings](../../../Examples/PrototypeBindings/README.md) 的配置方式；SDK 负责绑定、初始化与接收 worker，应用不重复操作 UART 或调用 Feed/Poll。

裁判通道要求 115200/8N1，并同时启用 RX/TX；系统配置预检和 `SRNOS_RefereeBindChannel` 共用校验。worker 使用 3072 B 栈，模块启动预算为 4 KiB，栈与预算常量一起维护。

## 读取示例

```c
#include <SirinEmbedOS/Services/Referee/referee.h>

bool read_referee(SRNOS_RefereeSnapshot *out)
{
    if (out == NULL || !SRNOS_RefereeReceiverReady()) return false;
    SRNOS_RefereeGetSnapshot(out);
    return SRNOS_RefereeIsOnline(0);
}
```

当前 `SRNOS_RefereeIsOnline(timeout_ms)` 的参数保留但不参与判断，内部按 `ROBOT_STATE` 的固定时效判断；不要以为传入 500 就更改期限。快照和在线检查不是同一原子事务。应用应按字段所属消息检查时效，不能把“近期收到任意帧”当成所有字段都已更新。设备健康、CRC 错误和溢出应单独诊断。

## 发送和任务约束

UI 布局、刷新频率和接收目标由应用决定。`SRNOS_RefereeTxWrite()`、`WriteStr()`、`Transmit()` 将数据复制到 Serial 队列；timeout 单位为毫秒，支持 `SRNOS_NO_WAIT`/`SRNOS_WAIT_FOREVER`。期限包含锁竞争和入队，成功表示复制接纳，不代表 UART 线发送完成。调用者缓冲在成功返回后可复用；阻塞发送只允许任务上下文，实时回调应使用有限等待。

发送通路随 SDK 的裁判串口绑定完成初始化，不需要单独的 TX 初始化调用；`SRNOS_RefereeTxSend()` 为零等待提交。

`SRNOS_Referee` 为优先级 47、3072 B 栈，低于 CAN1/2/3 与 IMU/INS，接收采用托管单轮与相对等待，无独立 TX 线程。普通等待不计入循环统计，处理内阻塞、抢占和中断计入墙钟。

## 诊断

使用 `SRNOS_RefereeReceiverReady()` 检查接收装配，`SRNOS_RefereeIsOnline(0)` 检查近期机器人状态帧，`SRNOS_RefereeGetOverflowCnt()` 检查溢出；匹配 ELF 的 Debug Watch 可辅助定位 CRC/发送队列。主机编译和结构断言不能证明实际裁判链路或 UI 显示。修改与验证记录见 [CHANGELOG](CHANGELOG.md)。
