# Changelog

## Unreleased

- 2026-09-28：公共接口只含 SDK 类型，固定 BMI088 寄存器、初始化和 SPI 片选移为私有实现。父项目通过 `SRNOS_SensorGetBMI088SPIStatistics()` 获得两颗片选的 SPI 统计副本，不引用驱动私有头；其他采样快照与频率保持。名称/路径迁移见 `Docs/SDKMigration.md`。 本轮主机/构建范围见 `Docs/Validation/SRNOSBoundary-20260928/README.md`；未重新烧录。

- 2026-09-28：BMI/LSM/环境采样拆为单轮和私有等待，统一 SRNOS_ 名称；保持周期超期重置及 BMI DMA 等待。 主机/构建证据见 `Docs/Validation/ManagedTasks-20260928/README.md`；本次未烧录，时序与开销仍待板测。

- 固定绑定与坐标关系移入 Board/Support/Devices；采样、总线优先级和发布路径不变。 本次主机及构建验证见 Docs/Validation/BoardSupport-20260928/README.md；未板测。 · 2026-09-28

- LIS/SPA 改用 I2C4 分发队列的保留槽和 30 ms 完成等待，支持 EEPROM 大量分段读写时继续周期采集。
- 保留环境线程数量、Normal 优先级、2 KiB 栈、10 ms 默认轮询及器件配置；新增模块说明和并发板测记录。
