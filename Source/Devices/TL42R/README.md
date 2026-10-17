# TL42R 设备访问

公共头为 `SirinEmbedOS/Devices/TL42R/TL42R.h`，通过 SDK RS485/Modbus 访问。父项目先将 UART7 声明为 RS485，在 `bind_channels` 回调中声明 TL42R 地址；注册启动后冻结。设备对象须保持到固件使用结束。

```c
#include "SirinEmbedOS/Devices/TL42R/TL42R.h"
static SRNOS_TL42RDevice drive;

// 由 bind_channels 调用；前提是 UART7 已配置且协议地址 1 未占用。
int bind_drive(void) {
    return SRNOS_TL42RInit(&drive, SRNOS_RS485_ROUTER_PORT_UART7, 1);
}
// 运行期任务调用，只有返回 SRNOS_TL42R_OK 时使用 monitor。
int read_drive(SRNOS_TL42RMonitor *monitor) {
    return SRNOS_TL42RReadMonitor(&drive, monitor, 30);
}
```

示例只读取监控，不使能或移动电机。设备事务限任务上下文，等待单位为毫秒；请求与应答缓冲生命周期遵循 [RS485](../../IO/RS485/README.md)。寄存器权限和类型可用参数描述接口查询，写入须满足对应范围；电机使能、动作和控制策略由父项目决定。

调试全局为 `SRNOS_DebugTL42RWatch` / `SRNOS_DebugTL42RConfigCache`。主机协议回归入口为 `bash Tests/Serial/run_host_tests.sh`（SDK 根）；具体设备互通需实机验证。
