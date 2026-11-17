# Changelog

## Unreleased — Clock 与 NTP

- 2026-10-21：增加 Clock 绝对时间映射与 Network 可选 SNTPv4 客户端；无授时仅维护 uptime，失联保持本地推算，现有控制计时不变。新增公共头和网络结构须整体重编译，无新增任务。

## Unreleased — 输入与消息

- 2026-10-21：增加 Input、Message、SirinRMT；物理输入与通用消息分离，各传输不再共享单一命令消费者。

## 2026-10-20

- 纳入可选 VideoLink 组件与公共导航，复用 RemoteControl worker 和 Telemetry 缓存；新增专项主机测试并接入汇总 runner。行为与验证见 VideoLink/RemoteControl CHANGELOG。

## Unreleased

- 2026-09-29：增加统一文档导航和存储/HostLink 专题入口，区分当前合同与历史验证；无服务运行变更。

- 2026-09-29：实现上位机手动静态 IMU 陀螺零偏校准（任意静止姿态），保留有效加速度校正，约 5 s 观察及异常拒绝后保存单份 EEPROM，新值下次启动生效。新增 CONFIG 0x09/0x0A、完整错误/部分保存状态，复用 HostCmd，无新增线程或动态工作区；验证范围为主机与构建，未烧录或实板验证。

- Storage统一单份EEPROM记录；Parameters固定业务值与一致事务，Telemetry独立最新值缓存并由HostComm编码。启动加载/保存复用现有线程，源V2 10–14支持1kHz及8000标量/s预算。实际吞吐、掉电与CAN组合开销待板测。

- 2026-09-28：统一 SRNOS 公共能力及私有服务装配，HostLink 活动事务/重放/待发送结果分离并按事件唤醒，日志有界非阻塞，遥测逐条短快照；协议与线程分离保持。新增固定 D1 状态计入本轮 map 记录，运行时负载与 CDC 拥塞仍待板测。

- 2026-09-28：服务统一接入托管循环与 SRNOS_ 命名，保持行为与等待；新增循环快照与遥测。验证范围为主机与构建；本次未烧录，时序与开销仍待板测。

- 2026-09-28：SystemMonitor替代Diagnostics线程，1 Hz ADC/任务统计与HostLink发布；增加ADC板级BDMA接入、逐槽统计和主机工具，默认取消扫栈。Indicators/生成文件不变。验证范围为主机与构建，未烧录/板测。

- 2026-09-28：新增 Indicators 服务归属，USB 服务通过 Support 准备硬件；现有 API、调度和重试语义保持。21 组主机测试及固件集成验证通过，未板测。
