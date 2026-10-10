# IMU 转台编码器 BringUp：2026-10-09

新增 `Examples/IMUTurntableBringUp/app_main.cpp`，在 CAN3 / Classic 1 Mbit/s 注册 GM6020 ID 1、2、3，反馈标准 ID 为 0x205、0x206、0x207。适配器 `poll_tx` 和 `commit_tx` 均为空；没有运动、使能或零命令发送路径。用户明确授权烧录和验证，并要求不驱动电机。

## 构建与烧录

示例/电机/Telemetry 三组主机回归、根 Debug 固件、ELF 内存/向量、源码结构及完整 12 目标父工程检查通过。父工程检查包含 93 个公共头、162 个 C11/C++17 编译/依赖闭包单元、15 条应用 include 隔离检查。没有修改 CubeMX 生成内容、SDK 公共 API 或链接内存布局。

连接前确认 CLion 无调试会话、无 GDB/J-Link 控制者及 CDC1 占用。使用 J-Link V8.96、探针 10114514、SWD 4 MHz；VTref 3.30 V，目标 STM32H723VG。先完整备份 1 MiB Flash，再下载 `build/IMUTurntableBringUp/SirinEmbedOS.elf`，向量、text、rodata 比对一致，复位运行。ELF SHA256 为 `66070b9a10fe99e70b7ebbe997452114269eca8be3411ca47ac9f4483aa21a3e`；源码和备份哈希见 [manifest](manifest.json)。原 Flash 备份保存在本地 `build/IMUTurntableBringUp/board-validation/flash-before.bin`。

## 实际反馈与 HostComm

使用明确的 CDC1 `/dev/cu.usbmodem355C343932333`，主机脚本只发送 SOURCE V2 LIST、SUBSCRIBE、CANCEL_ALL，不发送参数、存储或控制命令。9 个只读源全部可枚举并订阅；每颗电机原始编码器和角度各 1000 Hz，在线状态 100 Hz。

第一次完整采集持续 30.000268 秒，六个编码器/角度源各收到 30,000 条，三个在线源各收到 3,000 条。全程有效且在线，解码错误为零。期间角度明显变化，用户明确确认是手动转动转台；不能把这些变化记作固件驱动电机。见 [手动转动采集](capture-complete.json)。

为排除调试暂停对接收统计的扰动，再次复位后连续运行，采集期间不暂停：

| 项目 | 结果 |
|---|---|
| 观测窗口 | 30.000459 秒 |
| 编码器/角度记录 | 每源 29,999 条，约 999.95 Hz |
| 在线记录 | 每源 3,000 条，约 100 Hz |
| 有效性 / 在线 / 协议解码 | 无无效样本，三颗全程在线，无解码错误 |
| ID 1 编码器范围 | 7270～7271，319.4824～319.5264° |
| ID 2 编码器范围 | 4823～4824，211.9482～211.9922° |
| ID 3 编码器范围 | 4044～4045，177.7148～177.7588° |

29,999 与名义 30,000 的差异来自订阅/读取窗口边界；以实际墙钟时间计算频率，SOURCE 为最新值缓存而非无损 CAN 逐帧记录。见 [不中断采集](capture-uninterrupted.json)。

窗口结束后第一次暂停读取：HAL tick=45071 ms、RTOS tick=45059，系统 RUNNING，native CAN/USB ready。CAN3 ISR 接收 135,171 帧，router 已处理 135,170 帧，暂停瞬间尚有 1 帧未分发；无丢弃、软件/硬件接收溢出、错误或 Bus-Off。原生三路 `tx_ok_cnt` 均为 0；CAN3 one-shot/urgent/periodic 接纳、发送完成均为 0。这证明本固件没有提交电机控制帧，不等于进行了独立总线分析仪抓包。

三个适配器实际 `poll_tx/commit_tx` 均为 null；三颗电机均 online，结束快照 speed_raw=0。CFSR/HFSR 为 0。前一完整窗口后的 `TurntableView` 状态为 RUNNING，2048 B 栈未触及区 1604 B；这仅代表此次观测，不是任意负载保证。所有读取见 [GDB 原始记录](gdb.log)。

## 中间问题与未验边界

- 首次探索采集因验证脚本假定 `sampleTimeUs <= encodeTimeUs` 而中止。核对 HostLink 实现可见编码时间在读各标量缓存之前获取，应用可在其间发布新值，因此该断言不成立。本次只修正采集脚本并记录差值；未修改固件协议。第一完整窗口最大正差为 18 µs。保留原始探索日志于本地 `board-validation/capture.log`。
- 中途暂停调试后曾看到 2 次硬件 RX message-lost 事件，不能把那个累计窗口描述成零溢出；上述最终复核从复位起不中断，接收溢出计数为 0。
- 启动仍报告 `identity_load_status=SRNOS_STORAGE_CORRUPT`，与先前 CAN 板测记录一致；本次不处理身份存储，不写 EEPROM，不影响这 9 个只读编码器遥测源。加热和 INS 融合明确关闭。
- 未做电机控制、自动转台、IMU 校准求解/保存、绝对角度精度或三轴同步性验证；离线超时与恢复仅主机注入，未物理拔线。标量时间是应用快照观测时间。

## 结束状态与复现

新固件保留在板上。无断点遗留；GDB detach 后目标继续运行，退出本次 GDB 并停止本次 J-Link server，端口 2331 已释放。随后额外 5 秒 CDC1 采集仍全有效、三颗在线、解码无误；见 [释放后采集](capture-after-detach.json)。CDC1 订阅已取消、串口关闭。没有提交或推送代码。

[capture.py](capture.py) 可从仓库任意工作目录调用，需安装 pyserial，且明确指定本机 CDC1；输出目录必须不存在。原始 RX 字节、本地构建/烧录日志保存在 `build/IMUTurntableBringUp/`，摘要和哈希随本记录保留。复现只读采集示例：

```sh
build/hostlink-venv/bin/python Docs/Validation/IMUTurntable-20261009/capture.py \
  --port /dev/cu.usbmodem355C343932333 --output /tmp/turntable-new-capture --seconds 30
```
