# RemoteControl

本模块的公共接口位于 Include/SirinEmbedOS 对应能力目录；硬件接线由 Board/Support 持有。

## 托管线程

SRNOS_RC 接入托管单轮与相对 1 ms 等待，解码与仲裁行为保留。 当前 SRNOS_RC 为 P1（High7 / 47），与 CAN4/Serial 同级，低于 P0 CAN1/2/3 和 IMU/INS；栈、资源所有权和常驻线程数保留。私有等待适配由 Kernel 驱动；普通等待不计入 step，处理期间 I/O/抢占/中断计入墙钟。统计不能替代板端时序验收。详见根文档及 Source/Kernel/README.md。

## 公开接口

DR16、VT03 与仲裁接口统一使用 `SRNOS_*`，公开头位于 `Services/RemoteControl/`，不依赖 HAL/CMSIS/FreeRTOS。设备协议值和映射不变，Serial 通道由启动配置冻结，父项目只通过公开状态和仲裁 API 使用输入。旧头及名称已删除；迁移说明见 `Docs/SDKMigration.md`。
