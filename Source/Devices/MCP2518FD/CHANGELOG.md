# Changelog

## Unreleased

- 2026-10-06：新增自维护运行期FIFO访问，缓存冻结配置，状态/UA合读和按DLC搬运；保留UINC不确定在途对象并通过UA确认，供应商初始化源码不变。有限FIFO/回绕/逐阶段故障模型通过。联合验证结果与未验边界见 [CAN4 板测摘要](../../IO/CAN/CHANGELOG.md#can4-20261006)。

- 2026-10-06：新增Classic/FD真实CAN1↔CAN4负载证据。默认8M跨端点仲裁采样点不一致导致功能失败，CAN1测试专用80%对照通过；FD64B各3k时硬件RX溢出，SPI事务零超时并不等同零CAN丢帧。驱动及供应商代码保持原样。

- 2026-10-03：自维护外置 CAN backend 使用冻结配置选择 Classic mode 6 或 FD mode 0，分别有界确认配置/最终 OPMOD；仲裁 125/250/500 kbit/s、1 Mbit/s，BRS 数据 500 kbit/s、1/2/4/5/8 Mbit/s。无 BRS 数据等于仲裁，125/250 kbit/s 的未使用 data 寄存器采用 1 Mbit/s 占位。MCP 40 MHz、SPI 10 MHz 和供应商源码保持。
- 2026-10-03：FD FIFO 改为 16 RX＋12 TX、64 B payload，共 2016 B，关闭 TXQ/TEF/RX timestamp；Classic 保留 32/32、8 B。精确 DLC、64 B RX/TX、模式过滤、握手超时、逐事务失败、token 重试与共享 IRQ 计数由实际 vendor 寄存器 fake 回归。TEF 未启用，CAN4 完成统计仅表示确认的 TX 事件；PCB 离线，物理通信与时序未验。

- 2026-10-03：共享 SPI 中 MCP 优先类限定于低层：整体低于 IMU，低层内高于普通请求并有界让出；器件绑定、事务时序和供应商代码未改。

- 2026-10-03：SPIAdapter 为 MCP 绑定增加私有 CAN 优先类标记，使用共享 SPI4 的有界公平仲裁；未改供应商代码及硬件时序。默认/显式绑定主机回归通过，3 kHz 双向吞吐待验。

- 2026-09-28：自维护 SPI 适配改名为 `SPIAdapter.c`，容量/超时和 guard 采用 SRNOS 命名；供应商规定入口保持，板级绑定主机测试通过，未板测。

- 2026-09-28：完成 Support 职责整理；接口/时序/调度保持，固定硬件配置改为板级私有。21 组主机测试与固件集成构建通过；未板测。

## 优化前 CAN1/CAN4 实测基线（2026-10-06）

保持40MHz控制器和10MHz SPI，Classic单向3k可交付，双向各2k整板CPU约91.5%；FD64B各3k出现RX FIFO溢出。默认8M与原生CAN仲裁采样点不一致，CAN1测试专用80%配置下格式检查通过，生产位时序仍待修正。未修改供应商实现，不将一次SPI成功等同于外部CAN交付。
