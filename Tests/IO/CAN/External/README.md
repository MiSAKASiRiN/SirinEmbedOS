# CAN4 设备与服务主机验证

运行 `sh Tests/IO/CAN/External/run_host_tests.sh`。实际 CAN4 device/RX/TX、MCP FIFO 路径和供应商初始化实现参与编译；替换 RTOS 边界及 SPI 寄存器/RAM，不复制帧装载或位时序算法。测试以 UBSan 执行。

有限深度模型按冻结配置维护 Classic 32/32、FD 16/12 FIFO、UA 环绕和 UINC/TXREQ。覆盖全部 DLC、STD/EXT 与模式隔离、初始化各阶段失败、RAM/状态/UINC 前后失败、UA 确认成功或安全重试、连续确认失败保留在途、异常 UA 隔离且不重放。队列满后拒绝，释放后可继续。

实际服务步进验证普通 4RX/4TX、首次读取发现半满立即 8RX/1TX、32 帧与 1ms 预算、持续 RX 时保留待办、休眠前已到事件与启动句柄发布竞争。实际 Classic adapter/TTL 的有界 TX 步进另由 Router 套件覆盖；SPI 序列各边界 IMU 插入、公平性及取消/迟到 DMA 由 SPI 套件覆盖。

这些是可重复的软件模型。ACK、电气时序、真实抢占与吞吐以板测记录为准，不由主机测试推出。
