# RS485

本模块的公共接口位于 Include/SirinEmbedOS 对应能力目录；硬件接线由 Board/Support 持有。

## 托管线程

SRNOS_RS485 接入托管循环，队列等待移到统计外，事务应答/静默等待仍计入墙钟，原批处理保留。 当前 SRNOS_RS485 为 P1（High7 / 47），与 CAN4/Serial 同级，低于 P0 CAN1/2/3 和 IMU/INS；栈、资源所有权和常驻线程数保留。私有等待适配由 Kernel 驱动；普通等待不计入 step，处理期间 I/O/抢占/中断计入墙钟。统计不能替代板端时序验收。详见根文档及 Source/Kernel/README.md。
