# TL42R 设备访问

公开头为 `SirinEmbedOS/Devices/TL42R/TL42R.h`，通过 `SRNOS_TL42R*` 和 SDK RS485/Modbus 访问。父项目启动时声明 endpoint、设备地址与参数，注册冻结；寄存器定义、Modbus 数据格式与设备命令保持。

调试全局使用 `SRNOS_DebugTL42RWatch` / `SRNOS_DebugTL42RConfigCache`，与同名数据类型区分。设备事务限任务上下文，等待单位为毫秒；请求与应答缓冲生命周期按 RS485 接口管理。电机使能、动作和控制策略留给父项目。

本轮为源码命名与依赖隔离，接口/构建检查见 `Docs/Validation/SRNOSBoundary-20260928/README.md`；没有新增板端验证。
