# H12H 串行舵机协议

公开头为 `SirinEmbedOS/Devices/H12H/H12H.h`，类型及函数统一为 `SRNOS_SerialServoH12h*`，宏为 `SRNOS_SERIAL_SERVO_H12H_*`。父项目在 bind_channels 声明 Serial 用途，创建协议对象并保存到使用结束；SDK 不默认控制外部舵机。

事务通过 SDK Serial 完成，timeout_ms 使用毫秒，调用限任务上下文；发送与应答期限遵循 Serial Transceive 合同。帧头、命令号、校验、位置量程和帧间隔不变。单线电气适配需要硬件确认，不能直接改 GPIO 假定支持。

本轮为源码命名与依赖隔离，接口/构建检查见 `Docs/Validation/SRNOSBoundary-20260928/README.md`；没有新增板端验证。
