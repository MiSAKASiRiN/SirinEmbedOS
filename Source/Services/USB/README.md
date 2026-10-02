# USB 服务

本目录负责双 CDC 通道、日志/HostLink 传输、描述符与协议栈生命周期；板级 PCD/FIFO/端点接入及 PA11/12 配置在 Board/Support/USB。`SRNOS_USBStart` 保留一次成功启动合同，先调用板级准备，再初始化通道、日志及类；准备失败直接返回，协议栈启动中途失败调用 DeInit，之后可重试。成功后重复调用返回 false。

时序和 FIFO 参数保持；USB 已启动不代表枚举/DTR/传输成功。主机验证注入准备失败、四个协议栈失败位置、重试和重复启动；实际 USB 连接需板测。

## 公共依赖与命名

父应用只调用公开 `SRNOS_USB*` 能力；CDC transport、描述符、PCD 和 class context 是 SDK 私有实现。自维护跨文件符号采用 SRNOS 命名，HAL/USBD_LL 规定回调保留原 ABI；USB PID/接口字符串、双 CDC 端点和线数据保持。Platform 不向父项目导出 middleware 或 HAL include。
