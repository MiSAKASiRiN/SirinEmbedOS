# SirinEmbedOS 文档导航

当前合同按 2026-09-29 源码整理。README/专题文档描述现在的用法；CHANGELOG 记录变化；`Validation/日期目录` 保存当时的固件与实验依据，保留历史内容，不据旧板测认定新功能已验收。

| 要做的事 | 入口 |
|---|---|
| 构建 SDK、父项目接入 | [根 README](../README.md)、[源码破坏性迁移](SDKMigration.md) |
| 创建/停止/删除线程、delay、同步 | [Kernel](../Source/Kernel/README.md)、[Minimal](../Examples/Minimal/README.md) |
| 设计 EEPROM 数据、恢复规则 | [持久化总览](Storage.md)、[Storage API](../Source/Services/Storage/README.md) |
| 声明 PID 等可写可保存参数 | [Parameters](../Source/Services/Parameters/README.md)、[参数示例](../Examples/Parameters/README.md) |
| 发布编码器等只读高频变量 | [Telemetry](../Source/Services/Telemetry/README.md) |
| 编写上位机 COBS/HostLink 通信 | [帧格式与消息表](HostLink.md#cobs-帧与校验)、[参数消息字段](../Source/Services/Parameters/HostLink/README.md) |
| 在父项目注册命令/流/控制邮箱 | [HostLink API](../Source/Services/HostLink/README.md) |
| IMU 原始采样、坐标、融合和静态校准 | [Sensors](Sensors.md)、[INS 合同](INS.md)、[静态校准用法](../Source/Services/INS/README.md#手动静态校准) |
| 读取 ADC、CPU 和循环统计 | [SystemMonitor](../Source/Services/SystemMonitor/README.md) |
| 开发机记录 IMU/INS/监测遥测 | [Tools/HostLink](../Tools/HostLink/README.md)；现有 CLI 不提供参数编辑或校准触发界面 |
| 原始 EEPROM 调试与全片维护 | [器件驱动](../Source/Devices/EEPROM/README.md)、[EEPROMBringUp](../Examples/EEPROMBringUp/README.md) |
| 核对资源/实测/未完成工作 | [验证索引](Validation.md)、[开发清单](Development.md) |
| 查 Board/CubeMX 与迁移所有权 | [Board Support](BoardSupport.md)、[来源与去向](Migration/README.md) |

## 最近完成的能力与验证边界

- 命名与父项目公共依赖隔离、托管任务、SystemMonitor：有主机/构建和限定范围板测；以各记录的 ELF/配置/负载为准。
- EEPROM 单份身份/校准/参数记录、Persistent 与只读 Value、SOURCE V2：已实现，见 [Persistence-20260928](Validation/Persistence-20260928/README.md)；新格式未做真实掉电验收。
- 任意姿态静止陀螺零偏校准及 CONFIG 09/0A：已实现，见 [StaticIMU-20260929](Validation/StaticIMU-20260929/README.md)；未烧录，保存的新校准下次启动应用。

新增公共合同先更新对应能力 README/CHANGELOG，再把跨模块交互加入专题文档。协议的数字字段以公共协议常量和编码实现核对，存储布局以 Storage 编解码核对；不要把服务号、消息号、流 ID、参数 index 和 stable_id 混用。
