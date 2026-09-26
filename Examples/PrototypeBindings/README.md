# prototype 串口绑定示例

将 `bindings.c` 加入父项目固件源文件，并由父项目实现 `SRNOS_ApplicationMain()`；也可与 SDK 的 Minimal 应用入口一起编译。

| 端口 | 初始化绑定 | 格式 |
|---|---|---|
| USART6 | DR16 接收 | 100000、8E2、RX 反相 |
| UART8 | 裁判系统收发 | 115200、8N1 |
| UART9 | VT03 接收 | 921600、8N1 |
| USART1 | 日志输出 | 115200、8N1 |

该映射以 prototype 的 `omx_uart_router_config.h` 为依据。端口格式、资源用途在初始化后冻结。SDK 默认不占用上述端口；此示例显式启用接收和日志，但不控制执行器。

原始串口、H12H 或 RS485 设备应在 `config->bind_channels` 中完成声明：先 `SRNOS_SerialGetChannel()`，再由协议初始化函数或应用调用 `SRNOS_SerialClaim()` 获取唯一所有权，保存成功获取的通道供运行期读写。已被服务占用的端口及启动后的 `GetChannel()` 返回空指针。已配置但没有所有者的端口会阻止启动。

H12H 默认未绑定；SDK 没有把任意 UART 引脚当作已经确认的单线接口。RS485 固定在 UART7，设备应共用该总线的事务调度器。串口切换软件绑定仍需实际接线与电气接口相容。
