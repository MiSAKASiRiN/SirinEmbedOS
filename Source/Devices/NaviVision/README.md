# 导航与视觉 CAN 适配

公共头为 `SirinEmbedOS/Devices/NaviVision/NaviVision.hpp`，类型位于 `SRNOS::Devices`。`Navi` 接收速度协议，默认标准 ID 为 `0x401`；`Vision` 接收从 `0x300` 开始的 14 个目标槽。ID、字段与缩放以公共头中的协议定义为准。

对象由父项目持有，在配置阶段注册到 Classic CAN Router；注册后不更改接收 ID，生命周期覆盖路由使用期。模块不创建业务线程，不触发运动或射击。以下为导航接入片段：

```cpp
#include "SirinEmbedOS/Devices/NaviVision/NaviVision.hpp"
#include "SirinEmbedOS/Kernel/Thread.h"
static SRNOS::Devices::Navi navi;

// 在 ApplicationConfigure 中调用并检查结果；CAN1 必须配置为 Classic。
int register_navi() { return SRNOS::Devices::Navi_AttachCan(1, &navi); }
bool read_fresh_navi(SRNOS::Devices::NaviState& out) {
    if (!navi.snapshot(out) || !out.is_navi_online) return false;
    return (uint32_t)(SRNOS_TimeMs() - out.last_rx_ms) <= 30u;
}
```

Navi 的在线标志不代替应用的年龄检查；上例 30 ms 是业务示意阈值。Vision 由 adapter 推进超时，消费者仍检查每个目标的 `is_appear`，不能仅以 `fire` 字段直接触发动作。
