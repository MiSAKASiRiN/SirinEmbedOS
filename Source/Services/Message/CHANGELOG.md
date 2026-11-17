# Changelog

## Unreleased - VT02/VT12（2026-11-01）

- 按官方 2024 V1.6.4 增加 115200/8N1 的旧图传 A5 RX；0304 发布独立键鼠 Input，0302 原样送独立 VT02 Message transport。与裁判常规链路、VT03/13 互不混用。
- 新增 vt02_port、公有解码/绑定接口、串口格式/冲突检查与 InputIO 堆预算；新增静态缓冲，沿用原任务、优先级与栈，SDK/父项目需整体重编译。无旧图传 TX 或运动映射。
- ASan/UBSan 主机验证覆盖官方 CRC 表生成的固定向量、所有分片边界、粘包、符号极值、CRC/错误长度/未知命令/嵌套载荷、随机流、实际 InputIO/Input/Message 分发、按键边沿、独立时效、UART 异常/恢复、时钟回绕及有界轮询；父项目 Debug/Release 编译通过。未烧录，未进行真实图传或板端时序验证。


## 2026-10-21 SirinLink 命名

- HOST 适配器与共享 packet codec 改用 SirinLink 公共类型、符号和头路径；HostComm 传输含义、通用消息封装和线路字节保持。四传输 SirinRMT DISABLE 分发、编译与公共依赖检查通过，未烧录。

## Unreleased

- 2026-11-11：UDP adapter 通过 Network 的固定私有回调接收数据报，移除 Network 对 Message 协议的直接依赖；不增加报文副本、PCB 或线程。UART 配置条件收敛到供 SystemValidate/Prepare 共用的私有校验函数。静态复核及固件编译结果见根 CHANGELOG；未运行主机测试或板端通信验证。

- 2026-10-20：增加启动注册、队列/回调分发、通用消息封装和 HostComm/UART/UDP/图传适配；有界资源、运行期零分配。主机回归与 SDK 示例编译验证，未烧录，物理链路及实时栈高水位待板测。
