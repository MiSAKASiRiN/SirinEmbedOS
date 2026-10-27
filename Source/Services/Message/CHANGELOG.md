# Changelog

## 2026-10-21 SirinLink 命名

- HOST 适配器与共享 packet codec 改用 SirinLink 公共类型、符号和头路径；HostComm 传输含义、通用消息封装和线路字节保持。四传输 SirinRMT DISABLE 分发、编译与公共依赖检查通过，未烧录。

## Unreleased

- 2026-10-20：增加启动注册、队列/回调分发、通用消息封装和 HostComm/UART/UDP/图传适配；有界资源、运行期零分配。主机回归与 SDK 示例编译验证，未烧录，物理链路及实时栈高水位待板测。
