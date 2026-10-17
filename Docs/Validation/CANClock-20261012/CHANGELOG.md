# 验证记录

本文保存对应归档的原始报告。日期、版本、通过范围和结束状态仅对当时现场有效，不表示当前板上状态。当前阅读方法见本目录说明文件。

---

<!-- 原报告：README.md；原文 SHA256：f42751ebca8f51701d6db5ed10a95e6d977451f0f5cc83a631ff34906b3540a1 -->

# CAN 与应用时钟统一，2026-10-12

MSwerve 父项目实战发现：SDK 原生 CAN 回调使用 HAL/TIM7 毫秒时间，而应用公开的 `SRNOS_TimeMs()` 使用 FreeRTOS tick。两者启动原点不同；约 12 ms 的领先量让应用无符号反馈年龄回绕。该问题属于 SDK 对外时间基准不一致；修复在 SDK CAN 层，机器人映射和状态查询协议留在父项目。

## 修复合同

- 原生 CAN1–3 和 MCP2518FD/CAN4 的 Classic/FD 接收分发、适配器 tick/poll、发送 TTL、调度/重试使用 `SRNOS_TimeMs()`；未绑定 backend 的 core 也使用同一时钟，避免启动过渡改变已入队截止点。
- CAN adapter 的 `now_ms` 是软件分发时刻，不是硬件总线到达时刻；由它生成的 `last_rx_ms` 可以直接与 `SRNOS_TimeMs()` 比较。调度前为 0，32 位毫秒回绕；HAL 和 DWT 仍有各自原点，不能交叉相减。
- 父项目删除三个 RX 重打时间戳包装，直接注册 SDK 电机解析回调。仅关闭 adapter 的发送与自动 tick，由应用每 10 ms 进行离线更新；DM4310 状态查询仍每 50 ms 一次。
- 不修改函数签名、HAL/RTOS 源码、CubeMX 输出、优先级、线程数量和内存段布局。底层 HAL 超时和独立诊断时间没有做全库替换。

## 主机与构建验证

测试刻意令 HAL 领先应用时钟 12 ms。修改生产实现之前，真实原生 backend 测试在 `ops->now_ms()==app_ms` 处失败（regression-before.log）；修复后下列命令均返回 0：

```sh
bash Tests/IO/CAN/Router/run_host_tests.sh
bash Tests/IO/CAN/External/run_host_tests.sh
bash Tests/IO/CAN/FD/run_host_tests.sh
bash Tests/IO/CAN/Native/run_host_tests.sh
bash Tests/Devices/Motors/run_host_tests.sh
python3 Tests/Build/check_sdk.py --public-headers-only
```

覆盖实际原生 RX/TX worker 的 Classic/FD 时间传递、启动零时刻、32 位回绕；Classic/FD backend 绑定前入队的 TTL；CAN4 实际收发/供应商寄存器模型、SPI 拒绝后 1 ms 重试与到期；六种电机的反馈、严格超过 100 ms 离线阈值、新反馈与旧 tick 并发、回绕。路由虚拟四路负载不能证明实际电气吞吐。

93 个公共头、162 个 C11/C++17 编译单元和依赖闭包通过。父项目 Debug/Release 构建通过。Debug FLASH 299648 B（此前 299808 B），RAM_D1 281536 B，ITCM 63104 B；未增加静态内存。Release FLASH 230592 B。

## 板端验证

父项目：`2027-EmbOS-MSwerve`；SDK 基线 `805f0452b0aaae1d943f26de3d60f38609b5dde7` 加本次未提交修改。STM32H723VG / J-Link 10114514 / 3.30 V / SWD 4000 kHz。没有既有调试器占用。下载后 `.isr_vector` 与 `.text` 比对匹配。

Debug ELF SHA256：`3b4e4e8241972b85e29f25ac8a92ed01748878d57f1e39f15ec661e9d154f88e`。

20 秒 USB 采集取得 20 组完整报告（应用时刻 4005..23005 ms，共 260 条电机记录），每组均为 **13/13 在线、mask=0x1fff**。DJI 反馈年龄 0..1 ms；两台 DM4310 在日志采样相位均为 50 ms，符合每 50 ms 请求一次的状态查询方式，不表示 50 ms 总线传输延迟。没有年龄回绕。

一次暂停快照：HAL `uwTick=23967`，FreeRTOS `xTickCount=23955`，两种时钟仍相差 12 ms；`robot_motor_monitor` 为完整偶数序号 4786，应用快照时间 23945 ms，13 个反馈年龄均正常。GDB 回调指针直接指向 SDK `gm6020_on_rx/c620_on_rx/dm4310_on_rx`，没有应用时间戳包装，证明修复来自 SDK。

两台 DM 请求各尝试/接纳 478 次，拒绝为 0；CAN1/2/3 原生 TX 成功数 478/0/478，仅与两路状态请求对应。adapter poll_tx/commit_tx 均为空，周期发送接纳数为 0。快照中三路 bus-off、RX 溢出/丢失、错误与恢复计数均为 0。发送的是只读 0xCC 状态请求，没有使能、失能或运动控制。

GDB detach 后关闭本次 J-Link server，探针与 2331 端口已释放；随后 USB 在应用 57005/58005/59005 ms 再次报告 13/13 在线，MCU 保持运行。暂停取证会扰动实时行为，暂停前窗口与暂停后证据分别保留。

## 证据与边界

- `summary.json` 汇总二进制快照及 USB 统计；`snapshot.log` 包含时钟、SDK 回调地址与计数。
- `motors-usb.log`、`after-detach-usb.log` 为现场原始日志；其行前缀来自日志服务自己的时间，`STATUS_QUERY ms` 为任务时间，两者不要求同原点。
- `router/native/external/fd/motors.log`、`public-headers.log`、构建和 flash 日志记录对应验证层次。
- 本次板测仅 CAN1–3 Classic 和已有 13 个电机。CAN4/FD 改动由主机模型覆盖，没有改动现场接线或进行 CAN4/FD 电气复测。
- 物理断线、约 49.7 天自然回绕及 Bus-Off 未在板上注入；超时、回绕和恢复逻辑由主机测试覆盖。不是运动、闭环或长时可靠性验收。
- 未自动提交或推送，父项目 submodule gitlink 仍指向原基线；必须保留当前 SDK 工作区差异。
