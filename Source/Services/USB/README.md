# 双 CDC USB 服务

本服务装配双 CDC、日志/HostLink 传输、描述符与协议栈生命周期；板级 PCD、FIFO、端点及 PA11/12 接线由 Board/Support/USB 持有。CDC0 用于 ASCII 日志，CDC1 用于 HostLink 二进制协议。

## 使用示例

在应用唯一配置入口开启需要的服务，SDK 在启动线程负责 `SRNOS_USBStart()`；应用不再次调用启动函数：

```c
#include <SirinEmbedOS/Kernel/System.h>

void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config)
{
    config->usb_enabled = true;
    config->logging_enabled = true;
    config->host_link_enabled = true;
}
```

应用发送日志使用 [Logging](../Logging/README.md)，业务命令及遥测使用 [HostLink](../HostLink/README.md)。不要直接操作 CDC transport、USB class context 或 PCD。USB 启动成功不等于主机已枚举、DTR 已打开或数据已送达。

## 生命周期与资源

内部启动先执行板级准备，然后初始化通道、日志和类；准备失败直接返回，协议栈中途失败执行 DeInit 后允许重试，成功后重复启动返回 false。固定 AL94 双 CDC class context 使用静态数组，EP0 SET_CONFIGURATION/Init 不调用分配器；新增 class 必须重新核对 IRQ 上下文、存储所有权和预算。

## 诊断示例

在 SDK 根目录执行：

```sh
bash Tests/Services/USB/run_host_tests.sh
# 已准备 HostLink Python 环境时，列出实际串口并区分 CDC0/CDC1：
python3 -m serial.tools.list_ports -v
```

主机回归检查启动失败、重试、重复启动和回调隔离；实际 USB 枚举、拔插和 PCD 时序需要板端验证。主机环境准备及串口工具见 [Tools/HostLink](../../../Tools/HostLink/README.md)，修改和历史结果见 [CHANGELOG](CHANGELOG.md)。
