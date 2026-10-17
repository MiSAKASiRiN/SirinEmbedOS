# 超级电容 CAN 适配

公共头为 `SirinEmbedOS/Devices/Supercapacitor/Supercapacitor.hpp`，使用 `SRNOS::Devices::SupCap`、`SupCapState` 和 `SupCapTxCommand`。ID 默认值由 `SRNOS_SUPCAP_TX_STD_ID` / `SRNOS_SUPCAP_RX_STD_ID` 定义。

父项目持有对象，在配置阶段接入 Classic CAN Router，并明确设置使能、功率和能量预算；不要在全局构造函数中注册。以下示例注册周期为 10 ms，保持电容输出与无线输出关闭：

```cpp
#include "SirinEmbedOS/Devices/Supercapacitor/Supercapacitor.hpp"
static SRNOS::Devices::SupCap capacitor;

int register_capacitor() { // 由 ApplicationConfigure 调用并检查返回值
    capacitor.set_enable_supcap(false);
    capacitor.set_enable_wireless(false);
    return SRNOS::Devices::SupCap_AttachCan(1, &capacitor, 10);
}
bool read_capacitor(SRNOS::Devices::SupCapState& out) {
    return capacitor.snapshot(out);
}
```

对象生命周期须覆盖路由使用期。一次性复位使用 `request_reset_once()`，由 adapter 发送成功确认后消费；应用不得把软件接纳等同于设备已经完成复位。原始功率/能量值的缩放遵循实际协议，控制任务根据新鲜反馈决定是否使能。硬件收发与电源行为的验证方法见 [验证指南](../../../Docs/Validation.md)。
