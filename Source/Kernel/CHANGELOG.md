# Changelog

## 2026-10-17 文档整理

- README 维护当前使用方式、示例、资源和限制；变更经过与验证结果集中记录于 CHANGELOG。

### 从 README 归档的历史记录

以下保留原记录表述，时间与适用固件以所链接证据为准，不表示本次重新执行验证。

新增 Tests/Board/ManagedTasks 独立父项目板测，覆盖 400 次协作创建/删除、TLS、清理次序、同步删除保护和资源耗尽恢复。统计开关对照、完整 hook 基准和限制见板端验证记录。

公共隔离后的新固件已复测四个完整窗口，每次 400 个生命周期、2846 项检查零失败。两组统计 CPU 差值约 0.99 个百分点，完整 hook 最大 0.68 µs；具体负载和未验收边界见 [当前记录](../../Docs/Validation/SRNOSBoard-20260928/README.md)。

Classic 1M/8B双向各3k完成3×60s＋600s，零丢帧/拒绝/TTL过期/硬件RX溢出，78个十秒CPU窗口最高53.9632%。双IMU/INS保持1kHz，含6k过载的完成间隔/采集/SPI排队峰值1143/151/112µs；FD五速率320格式用例和过载恢复通过，64B各3k在5/8M下CPU约61%。实测仅CAN1/CAN4短接，CAN2/3/UART同时满载与物理故障尚未覆盖；原Flash恢复并释放探针。详见 [最终验证与原始证据](../../Docs/Validation/CAN4-Optimization-20261006/README.md)。

`Tests/Kernel/run_host_tests.sh` 使用 pthread 假内核运行实际封装，覆盖生命周期、同步、回绕与统计；父项目接入和固件链接由 Tests/Build 检查。这些主机/构建结果不代替板测。已完成本板 Release 负载下的 hook/CPU/临界区、生命周期、INS 与 CDC 短测；外部 CAN 负载、长时间压力和 Debug hook 时序仍待验收，详见 [板端记录](../../Docs/Validation/ManagedTasksBoard-20260928/README.md)。

## Unreleased — 静态 IPv4

- 2026-10-14：固定 lwIP 2.2.1 / IPv4+ARP+UDP+ICMP，默认 IP 192.168.1.50/24；网络、可选网关和 UDP 端口由上层启动配置冻结。Network 单线程持有协议栈和二层收发，不改变 MCO/RMII/DMA/IRQ。新增 6 KiB 栈、7 KiB heap 预算及约 22 KiB 静态 D1；明确排队期限、link epoch 和原始接口所有权。主机/构建及父项目板测记录见 Network 文档。


## Unreleased — Ethernet 二层接入

- 2026-10-13：新增 Ethernet 冻结配置、LAN8720A 链路管理、原始帧 API 与低优先级 worker；ETH IRQ 7、D2 DMA 新增 9216 B，现有时钟/MPU/生成区不变。默认关闭，父项目显式开启；详见 Source/IO/Ethernet 文档与父工程实测记录。

## Unreleased

- 2026-10-10：HostComm 栈增加 1 KiB，HostLink 保守启动预算 8→9 KiB；线程数量、优先级及 heap 分区不变。 Debug 构建及板端启动/保存通过，见 [验证记录](../../Docs/Validation/IMUTurntableSaveFault-20261010/README.md)。

- 2026-10-06：CAN4收发合并为一个SRNOS_CAN4，CAN线程数8→7、CAN4栈预算8→4KiB；保守基础启动heap预算64→60KiB，P0/P1与IMU线程不变。 Classic目标3×60s＋600s通过，最高十秒CPU53.9632%，IMU保护线通过；FD格式/负载/恢复、资源及原Flash恢复见 [最终记录](../../Docs/Validation/CAN4-Optimization-20261006/README.md)。

- 2026-10-03：SystemConfig.can[4] 新增每控制器启动期 Classic/FD 互斥配置，默认 Classic 1 Mbit/s；应用显式声明 FD 仲裁/数据/BRS，内核初始化前完成兼容校验、复制与冻结。复用既有八个 CAN worker 与 P0/P1 优先级、线程栈及 heap；FD 固定 32 订阅/64 TX 槽每路，四路上下文默认 23312 B 在 D1 BSS。配置冻结/资源准备与硬件就绪分开，CAN4 成功初始化才发布通知句柄；硬件离线待测。

- 2026-10-03：P0 为 CAN1/2/3 收发与 BMI/LSM/INS（Realtime / 48），P1 为 CAN4 收发及 UART 的 Serial/RC/Referee/RS485 worker（High7 / 47）。共享 SPI4 分发者保持 Realtime1（49），I2C4 保持 High6（46）。父应用1..39、周期、栈及任务数量不变，板端混合负载仍待验。

- 2026-10-03：CAN 八线程统一 Realtime（48），SPI4 Realtime1（49），Serial 分发 High5（45），IMU/INS 及 I2C4 原值保持。无新增任务、无栈/heap 扩容，父应用优先级合同不变；CAN index 0 直接通知与生命周期 index 1 分离。主机/构建结果见 CANRouter-20261002，组合负载未板测。

- AppInit接入有界身份/校准/参数加载，默认开启持久化与HostComm SAVE/LOAD，工厂身份写默认关闭；应用参数归属由稳定ID/版本声明。INS温控持续、校准就绪独立门控，无新增生产线程或优先级变更；板端启动及存储压力待测。

- 2026-09-28：公共隔离后固件复测四次 400 生命周期/2846 检查均零失败；统计新增约 0.99 个百分点，需保留余量优化。板测适配独立编译，应用无原生依赖；见 Docs/Validation/SRNOSBoard-20260928。

- 2026-09-28：Kernel 的 RTOS/CMSIS、Board 与 Source include 都是 SDK 私有编译依赖。生产应用只链接 Platform 并包含 SDK 公共头；需要原生 RTOS 的验证代码单独编译为测试适配目标。接口独立 C11/C++17、依赖闭包和父项目真实编译命令由 Tests/Build 检查。 本轮主机/构建范围见 `Docs/Validation/SRNOSBoundary-20260928/README.md`；未重新烧录。

- 2026-09-28：完善父项目管理指南：启动列表不返回句柄/不保证晚于 ApplicationMain，运行期创建与回收示例、DelayUntil 时钟域、init 未完成状态、受限批次事件处理示例及旧接口迁移。 文档中根 C++17 / Kernel C11 示例语法及本地链接检查通过。

- 2026-09-28：新增独立父项目真实 RTOS 板测；生命周期/TLS/同步删除保护/资源耗尽及循环统计成本对照见 Docs/Validation/ManagedTasksBoard-20260928。

- 2026-09-28：新增托管任务/时间/同步接口、启动预留池与协作删除，迁移 SDK 创建与命名并接入循环统计。 主机/构建证据见 `Docs/Validation/ManagedTasks-20260928/README.md`；本次未烧录，时序与开销仍待板测。

- 2026-09-28：SystemMonitor替代Diagnostics线程，1 Hz ADC/任务统计与HostLink发布；增加ADC板级BDMA接入、逐槽统计和主机工具，默认取消扫栈。迁移system_monitor_enabled及HostLinkPrepareEx合同；Indicators/生成文件不变。主机与构建验证见Docs/Validation.md，未烧录/板测。


- 指示器改经 Services/Indicators 私有头接入；线程配置与初始化顺序保持，板级 heap/入口移入 Support/System。 本次主机及构建验证见 Docs/Validation/BoardSupport-20260928/README.md；未板测。 · 2026-09-28

- I2C4 改为四槽分发及每槽完成信号量，保守启动同步预算增加 512 B；未增加生产线程或改变线程栈、优先级与 heap 分区。
