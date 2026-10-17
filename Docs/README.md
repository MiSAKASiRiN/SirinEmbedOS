# SirinEmbedOS 文档导航

第一次使用先读 [QuickStart](../QuickStart.md)，按完整示例构建一个父项目。本文按开发任务选择下一份指南；修改历史仅放各级 CHANGELOG，原始测试数据仍保留在验证目录。

| 要做的事 | 使用说明与示例 |
|---|---|
| 构建 SDK、创建父项目 | [QuickStart](../QuickStart.md)、[根 README](../README.md) |
| 升级旧 API 或项目结构 | [SDKMigration](SDKMigration.md) |
| 创建、停止、删除任务，使用同步对象 | [Kernel](../Source/Kernel/README.md)、[Minimal](../Examples/Minimal/README.md) |
| 使用 CAN、adapter 或 FD | [CAN](../Source/IO/CAN/README.md)、[Motors](../Source/Devices/Motors/README.md)、[CANFD 示例](../Examples/CANFD/README.md) |
| 使用 Ethernet、UDP、Ping | [Ethernet](../Source/IO/Ethernet/README.md)、[Network](../Source/Services/Network/README.md) |
| 配置 UART、遥控和 RS485 | [Serial](../Source/IO/Serial/README.md)、[PrototypeBindings](../Examples/PrototypeBindings/README.md) |
| 读取传感器、姿态和温控状态 | [Sensors](Sensors.md)、[INS](INS.md) |
| 声明 PID 等可写可保存参数 | [Parameters](../Source/Services/Parameters/README.md)、[参数示例](../Examples/Parameters/README.md) |
| 发布只读变量 | [Telemetry](../Source/Services/Telemetry/README.md)、[示例](../Examples/Telemetry/README.md) |
| 使用 EEPROM 身份、校准和参数记录 | [Storage](Storage.md)、[Storage API](../Source/Services/Storage/README.md) |
| 编写上位机，注册命令和数据流 | [HostLink 协议](HostLink.md)、[服务 API](../Source/Services/HostLink/README.md) |
| 采集 IMU/INS/CPU 遥测 | [Tools/HostLink](../Tools/HostLink/README.md)、[SystemMonitor](../Source/Services/SystemMonitor/README.md) |
| 维护硬件绑定或 CubeMX 输出 | [Board](../Board/README.md)、[BoardSupport](BoardSupport.md) |
| 检查代码、运行验证、阅读历史证据 | [Development](Development.md)、[Validation](Validation.md) |
| 查询代码来源、许可和重命名 | [迁移索引](Migration/README.md)、[迁移历史](Migration/CHANGELOG.md) |

## 查找示例

例如接入 UDP：先在 Network 指南找到启动配置和收发片段，再用 QuickStart 的周期任务调用公开接口。不要把网络 worker 的回调直接当作控制任务，也不要重复启动平台服务。

在 SDK 根目录可查找接口及其示例位置：

```sh
rg -n 'SRNOS_UDPReceive|SRNOS_UDPSend' Include/SirinEmbedOS Source/Services/Network/README.md
rg -n 'SRNOS_ThreadCreate' QuickStart.md Source/Kernel/README.md Examples/Minimal
```

## 维护规则

README、QuickStart 和专题文档说明当前用途、配置、使用步骤、示例及限制；日期、修改原因、验证结果和待验事项只写 CHANGELOG。协议字段以公共常量和编码实现为准，存储布局以编解码为准。服务号、消息号、流 ID、参数 index 和 stable_id 不可混用。

历史实验只说明对应固件、配置及窗口，不代表当前板上状态。证据目录中的 README 用于阅读或复现，历史结论见其 CHANGELOG；日志与哈希不因文档整理而改写。
