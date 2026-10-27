# Changelog

## Unreleased

- 2026-10-06：在CAN1/CAN4真实共享SPI负载中逐样本观测双IMU；包含CAN4过载窗口，双IMU/INS仍为1kHz，无INS missing/missed。记录最大样本完成间隔和采集耗时，未改驱动、采样周期或优先级；不替代姿态精度/温稳及UART混合负载验证。

- 2026-10-03：BMI/LSM 采样提升至 P0 / Realtime（48），SPI 仲裁优先 IMU；环境线程不变，采样周期/栈/发布合同保持。验证范围为离线回归与构建，实时延迟待板测。

- 2026-09-28：公共接口只含 SDK 类型，固定 BMI088 寄存器、初始化和 SPI 片选移为私有实现。父项目通过 `SRNOS_SensorGetBMI088SPIStatistics()` 获得两颗片选的 SPI 统计副本，不引用驱动私有头；其他采样快照与频率保持。名称/路径迁移见 `Docs/SDKMigration.md`。验证范围为主机与构建；未重新烧录。

- 2026-09-28：BMI/LSM/环境采样拆为单轮和私有等待，统一 SRNOS_ 名称；保持周期超期重置及 BMI DMA 等待。验证范围为主机与构建；本次未烧录，时序与开销仍待板测。

- 固定绑定与坐标关系移入 Board/Support/Devices；采样、总线优先级和发布路径不变。验证范围为主机与构建；未板测。 · 2026-09-28

- LIS/SPA 改用 I2C4 分发队列的保留槽和 30 ms 完成等待，支持 EEPROM 大量分段读写时继续周期采集。
- 保留环境线程数量、Normal 优先级、2 KiB 栈、10 ms 默认轮询及器件配置；新增模块说明和并发板测记录。

## 共享SPI压力观测（2026-10-06）

CAN1/CAN4短接的Classic/FD真实负载，包括CAN4 RX FIFO溢出的过载窗口，双IMU与INS仍保持1kHz，INS missing/missed无增长。测试observer逐样本统计完成间隔和采集耗时，未改变驱动、优先级或温控。此结果不是CAN4吞吐、温稳或姿态精度验收。
