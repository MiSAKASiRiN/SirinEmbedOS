# Changelog

## 2026-10-17 文档整理

- README 按当前使用方式、示例和约束组织；不在使用文档追加版本战报。

## Unreleased

- 2026-09-29：增加统一文档导航和存储/HostLink 专题入口，区分当前合同与历史验证；无服务运行变更。


- 2026-09-29：实现上位机手动静态 IMU 陀螺零偏校准（任意静止姿态），保留有效加速度校正，约 5 s 观察及异常拒绝后保存单份 EEPROM，新值下次启动生效。新增 CONFIG 0x09/0x0A、完整错误/部分保存状态，复用 HostCmd，无新增线程或动态工作区；验证范围见 `Docs/Validation/StaticIMU-20260929/README.md`，未烧录或实板验证。


- Storage统一单份EEPROM记录；Parameters固定业务值与一致事务，Telemetry独立最新值缓存并由HostComm编码。启动加载/保存复用现有线程，源V2 10–14支持1kHz及8000标量/s预算。实际吞吐、掉电与CAN组合开销待板测。

- 2026-09-28：统一 SRNOS 公共能力及私有服务装配，HostLink 活动事务/重放/待发送结果分离并按事件唤醒，日志有界非阻塞，遥测逐条短快照；协议与线程分离保持。新增固定 D1 状态计入本轮 map 记录，运行时负载与 CDC 拥塞仍待板测。

- 2026-09-28：服务统一接入托管循环与 SRNOS_ 命名，保持行为与等待；新增循环快照与遥测。 主机/构建证据见 `Docs/Validation/ManagedTasks-20260928/README.md`；本次未烧录，时序与开销仍待板测。

- 2026-09-28：SystemMonitor替代Diagnostics线程，1 Hz ADC/任务统计与HostLink发布；增加ADC板级BDMA接入、逐槽统计和主机工具，默认取消扫栈。迁移system_monitor_enabled及HostLinkPrepareEx合同；Indicators/生成文件不变。主机与构建验证见Docs/Validation.md，未烧录/板测。


- 2026-09-28：新增 Indicators 服务归属，USB 服务通过 Support 准备硬件；现有 API、调度和重试语义保持。21 组主机测试及固件集成验证通过，未板测。
