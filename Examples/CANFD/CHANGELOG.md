# Changelog

## Unreleased

- 2026-10-03：新增纯公共 API 示例，CAN1 为 1M/4M BRS CAN FD，其他通道为 Classic 1M；初始化精确订阅扩展 ID，ApplicationMain 读取状态后返回。默认只接收，可通过目标编译定义启用一次 12 B 测试帧提交。
- 加入第 11 个父项目固件目标和 SDK 集成检查；示例固件编译链接、最终 ELF、14 条应用编译命令的公共头隔离及可选单帧分支目标编译通过。PCB 离线，未验证物理 FD 收发或吞吐。
- 原生 RX 使用 4 帧 pop 分块、32 帧调度预算，保持 4 KiB worker 栈；示例说明回调栈及非阻塞边界。
