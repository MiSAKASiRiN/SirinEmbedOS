# 遥控输入与仲裁

公开头位于 `SirinEmbedOS/Services/RemoteControl/`。DR16/VT03 接收由 SDK Serial 绑定和 `SRNOS_RC` worker 驱动；父项目在启动配置中声明 UART 用途及 `remote` 选择，示例见 [PrototypeBindings](../../../Examples/PrototypeBindings/README.md)。绑定在启动后冻结，应用不要自行轮询同一接收器或操作 HAL UART。

## 读取示例

```c
#include <SirinEmbedOS/Services/RemoteControl/RemoteControl.h>

bool read_remote(SRNOS_RC_State *out)
{
    if (out == NULL) return false;
    SRNOS_RC_GetSnapshot(out);
    const SRNOS_RC_Status status = SRNOS_RC_GetStatus();
    return status.online && !status.blocked;
}
```

快照与状态是两次独立查询，不是联合原子事务；控制侧还需按自身安全要求检查新鲜度、来源和使能条件。返回不可用时不要继续使用上次输出。开关先检查 `s_valid_mask`，通道使用 `SRNOS_RC_CH_*` 枚举；`idle`/`sticks_idle` 不等于应用已经停车。应用优先读取副本，不把公开的 volatile 调试状态作为跨字段一致快照。

源更新、Tick 和协议解码由 worker 持有；不从业务任务并发调用这些写入接口。自定义映射及仲裁需在配置阶段明确设置，并核对具体遥控器的物理通道。

## 调度与诊断

`SRNOS_RC` 为优先级 47、3072 B 栈，采用相对 1 ms 等待；普通等待不计入循环统计，解码内 I/O、抢占和中断计入墙钟。状态查询可检查 `active_src`、`last_ok_ms`、各来源在线情况及 `blocked`；协议帧计数可通过 [Watch](../../Diagnostics/Watch/README.md) 查看。

Serial 通道与并发约束见 [Serial](../../IO/Serial/README.md)。历史修改及验证结果见 [CHANGELOG](CHANGELOG.md)，设备在线、通道映射和安全动作仍需在实际接收器上分别验证。
