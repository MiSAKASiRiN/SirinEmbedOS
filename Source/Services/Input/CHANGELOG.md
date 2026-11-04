# Changelog

## Unreleased — VT02/VT12 SirinRMT

- 显式注册 `SRNOS_MESSAGE_VT02`/串口 endpoint 时，沿用四实例上限和既有会话/序号检查，将 CRC 有效的 `0302` 30 B 原始载荷解码为独立 Input。串口流重置清除该来源的控制有效性，保留去重状态；默认 VT02 键鼠/原始 Message 合同不变。
- 主机集成覆盖 A5→Message→SirinRMT 的四轴、ENABLE/DISABLE、CRC、异会话/重复包拒绝、UART 重置与超时；不代表真实图传持续速率或运动验证。

## Unreleased — 平面速度输入

- 新增 PLANAR_VELOCITY codec 与能力声明，三个分量完整更新、独立时效。通道/速度互斥；每次切换推进 motion_epoch，取消旧 ENABLE；同消费者周期的往返切换及 DISABLE 锁存均可观察。
- 有限值/保留字节、所有权、独立过期、类型切换及物理输入回归通过 ASan/UBSan；C/Python 黄金向量一致，七来源示例保守 heap 预算 192840/196608 B。父项目主机控制链与 Release 通过；Jetson 100 Hz、直接/通道两类六方向与切换/停止实测通过，原始记录由父项目保管。

## 2026-10-21 DISABLE

- SirinRMT v1 增加 kind=5、零 body 的 DISABLE；Frame.request_disable 与 Snapshot.disable_sequence/ms/epoch 独立保留停止意图，清除旧 enable_epoch，不清空字段、不刷新字段时间。互斥 ENABLE/DISABLE、失效/重新同步隔离及父项目停止优先行为已覆盖。
- Codec、四传输真实分发、乱序/重复/异会话拒绝、字段保留/时效与后续输入不能覆盖停止请求的 ASan/UBSan 回归通过；C/Python 黄金向量一致。公共结构需整体重编译；七来源 heap 预算 192672/196608 B。未烧录。

## Unreleased

- 2026-10-21：增加 SirinRMT ENABLE（kind=4、零 body）及 Frame.request_enable、Snapshot.enable_sequence/ms/epoch。SDK 仅传递一次请求意图；失效/重新同步隔离旧请求，通道/按键与请求互不续期。公共结构变化要求 SDK/父项目整体重编译。
- 请求、序号拒重、STOP/恢复隔离、C/Python 黄金向量及物理解码适配通过 ASan/UBSan；七来源示例主机 heap 预算增至 192616/196608 B。父项目 Debug/Release 链接及公共头检查通过；未烧录，真实输入延迟和栈水位待板测。

- 2026-10-20：增加独立来源的通道/按键/相对量快照及事件环；实现 SirinRMT v1、会话/序号检查和各字段独立过期。主机回归与 SDK 示例编译验证，未烧录，物理链路及实时栈高水位待板测。

- 主机验证：Input/Message/SirinRMT 通过 ASan/UBSan，覆盖 codec 黄金向量、快照/过期/按键环、多传输会话、真实 DR16/VT03/W.Bus 每帧输入及错误恢复；七来源示例 heap 预算192504/196608 B（主机配置测试），目标布局以 ELF/map 为准。
