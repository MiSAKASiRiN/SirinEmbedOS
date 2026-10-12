# 迁移验证记录

## 2026-10-12 CAN 与应用毫秒时钟统一

MSwerve 父项目发现原生 CAN HAL 时间领先公开 RTOS 时钟约 12 ms，导致反馈年龄回绕。CAN1–4 Classic/FD 回调、TTL 与重试已统一到 `SRNOS_TimeMs()`，父项目移除重打时间戳包装。Router/Native/External/FD/Motors 主机套件、公共头与父项目 Debug/Release 构建通过；板上 CAN1–3 Classic 连续 20 组报告 13/13 在线，DJI 年龄 0..1 ms、DM 查询年龄 50 ms，无回绕。CAN4/FD 本轮仅主机模型验证，未做物理断线/回绕注入。固件仅状态查询，GDB/J-Link 已释放，未提交/推送。详见 [完整记录](Validation/CANClock-20261012/README.md)。

- 2026-10-10～11：[默认 USART1 Panic、转台时延和 CPU](Validation/IMUTurntableLatency-20261010/README.md)：Panic 物理输出通过；高频记录 CPU 中位34.69%，低流量承重23.94%；六段 ID2 运动相对时延和局限见记录。

- 2026-10-10：[恢复三轴直接双环全行程](Validation/IMUTurntableFullRestore-20261010/README.md)：回默认、ID1/2整圈往返、ID3端点扫转完成；完整电机/双IMU数据，最终默认位闭环保持。

## 2026-10-09：直接双环、ID1 54°/s与超速抗抖动

按用户要求取消虚拟轨迹，直接位置P→速度PI，速度限幅54/27/27°/s、输出25000；低阈值超速改为连续50ms确认。最终仅做针对性检查及编译，上板完成三轴回位和ID1约54°/s部分正转；raw3787处满输出仍停滞，3秒13raw触发全零。完整行程未完成，板端保持Fault7且探针/串口释放。详见[直接双环记录](Validation/IMUTurntableDirect-20261009/README.md)。

## 2026-10-09：转台25000上限、9°/s实测

已烧录三轴25000/9°/s，保留3°/s²加速度、15°/s估计超速及原5rpm等保护。修复虚拟轨迹在提速后停止距离过大导致跟随误差的问题。最终ID3约18.45s回位、三轴回位成功；ID1约151.7°在25000仍停滞，3秒2raw触发全轴归零，温度采集末49°C。完整扫转未完成；双IMU部分比较、固件/测试与释放证据见[本轮记录](Validation/IMUTurntableFast-20261009/README.md)。

## 2026-10-09：转台完整扫转与输出上限尝试

三轴3°/s、ID1/2各361°往返、ID3内缩4264/2096跨零往返已实现。ID3实板回位约42～45秒，比前版94秒快2.1～2.3倍，三轴回位成功；ID1两轮均在约150°受阻，8000提高至10000后仍未通过，新3秒净位移堵转保护已实板归零。完整整圈/ID2/ID3扫转未完成；双IMU部分段比较及全零释放证据见[本轮记录](Validation/IMUTurntableFullSweep-20261009/README.md)。

## 2026-10-09 IMU 转台自动回位与运动尝试

只接收示例已扩展为上电自动回位/低速观测，默认350/4850/5140，ID3跨零限位及首次启动恢复。主机模拟、故障注入、Debug构建和十二目标父工程检查通过；板端确认启动可抬升和越界/速度故障后全轴归零，ID1方向已用小输出正反核验修正。ID3已到5140并保持，ID1回位时仍触发超速保护，尚未完成三轴到位及ID1往返；双IMU已有ID3抬升和ID1部分回位段的初步对比，不能宣称校准完成。过程与证据见[运动记录](Validation/IMUTurntableMotion-20261009/README.md)。

## 2026-10-09 IMU 转台编码器 BringUp

新增 CAN3 三个 GM6020（ID 1/2/3）只接收示例和 9 个 HostComm 只读源。主机回归、Debug/ELF/结构及完整十二目标父工程通过；授权烧录后两段 30 秒采集全有效、三颗全程在线，最终不中断窗口发送/溢出/Bus-Off=0。中途角度变化由用户确认是手动转台。新固件留板运行，GDB/J-Link/串口已释放；校准算法尚未实现。详见 [原始证据与限制](Validation/IMUTurntable-20261009/README.md)。

## 2026-10-06 CAN4 CPU与丢帧优化最终验收

Classic 1M/8B双向各3k完成3×60s＋600s，零丢帧/拒绝/TTL过期/硬件RX溢出，78个十秒CPU窗口最高53.9632%。双IMU/INS保持1kHz，含6k过载的完成间隔/采集/SPI排队峰值1143/151/112µs；FD五速率320格式用例和过载恢复通过，64B各3k在5/8M下CPU约61%。29组主机/完整SDK/Debug与Release通过；原Flash完整恢复、运行验证完成、探针释放。实现、全部分档、失败中间版本、哈希与原始数据见 [完整记录](Validation/CAN4-Optimization-20261006/README.md)。

## 2026-10-06 CAN1 / CAN4 共享 SPI 与 IMU 压力

保持SPI10MHz、P0/P1及正式传感器负载。Classic单向3k通过，双向各2k CPU91.54%，请求各3k时生产者错过周期；FD5M/8M对齐配置64B双向各3k均有MCP RX FIFO溢出，不能验收CAN4 3k收＋3k发。双IMU/INS保持1kHz、SPI未报错或超时。默认FD8M首帧失败，测试专用CAN1仲裁采样点80%对照通过，生产时序未改。完整分档、对照、原始证据及恢复记录见 [CAN4/SPI板测](Validation/CAN4-SPI-20261006/README.md)。

## 2026-10-06 CAN1 / CAN3 Classic 与 FD 实板负载

用户短接CAN1/3后完成六模式、356功能帧与54负载窗口。Classic8B、FD8M64B每路同时3k收＋3k发连续60秒零丢帧/数据错误，整板CPU32.00%/35.13%；FD5M64B同档10秒通过，4M及以下64B背压/TTL未达标。双IMU/INS各1kHz连续，原生RX最小栈余3092 B。身份记录CORRUPT、LIS部分窗口低于100Hz另记待查；未写EEPROM，原Flash1MiB完整恢复并读回一致、探针释放。CAN2/4、UART混合负载及物理Bus-Off未测。见 [完整报告及原始数据](Validation/CANFD-Board-20261006/README.md)。

## 2026-10-03 CAN Classic / FD 互斥配置

四路控制器支持应用在初始化显式选择Classic或FD并冻结，现有adapter仅Classic1M。新增独立FD原始路由/64B发送、原生80MHz时序与共享RAM规划、CAN4 FD FIFO；修复原生RX栈批量搬运余量不足。29套主机、11父项目、93公共头/162编译单元、Debug/Release和ELF布局检查通过，FD静态池23312B在D1，ITCM/heap不变。PCB离线，吞吐、电气速率和混合负载仍待板测。见 [完整记录](Validation/CANFD-20261003/README.md)。

## 2026-10-03 P0/P1 优先级调整

P0 为 CAN1/2/3 收发与 BMI/LSM/INS（Realtime / 48），P1 为 CAN4 收发及 UART 的 Serial/RC/Referee/RS485 worker（High7 / 47）。共享 SPI4 分发者保持 Realtime1（49），I2C4 保持 High6（46）。NVIC 的 FDCAN1–3、IMU EXTI9_5、SPI4 及其 DMA 为 5；MCP EXTI15_10、五路 UART IRQ 及十路 UART DMA IRQ 为 6。共享 SPI 改为 IMU 优先并保留两级防饥饿；完整离线证据见 [记录](Validation/Priority-20261003/README.md)。PCB 离线未烧录，实际组合负载待验。

## 2026-10-03 CAN Router 离线优化

基线已提交为 820b777；按每路同时 3 kHz RX＋3 kHz TX 优化通知、路由、SPI 仲裁与优先级，并修复满环竞争、Bus-Off 和电机快照并发。28 套主机测试、十个父项目、91 公共头/158 编译单元、Debug/Release 及 ELF 布局通过；Release 命名工具保留 13 条既有标准库/TLS 误报。CubeMX 优先级生成成功，415 个 USER CODE 区保留。PCB 离线未烧录；四路虚拟负载只验证数据/计数，真实吞吐和混合负载时延待测。见 [完整记录](Validation/CANRouter-20261002/README.md)。

## 2026-09-29 ITCM RAM 迁移

CAN Router 上下文和 Debug Watch 正式迁入 ITCM，Release 释放 D1 59904 B、D2 2848 B；ITCM 余 2528 B，heap/线程/队列容量不变。25 套主机、十个父项目与 Debug/Release 通过。板端预填后软件复位清零、代码复制、Watch 读取、CDC 监测及五流短测通过；CAN 未短接，收发压力与实际断电未验。新 Release 留板运行，EEPROM 未改，调试连接释放。见 [记录](Validation/ITCM-20260929/README.md)。

## 2026-09-29 静态校准阈值复测

提高原始噪声拒绝阈值后，两次双IMU静态校准均成功，各501样本、约5.3秒，活动请求重试未重复执行。软件复位加载revision2，与返回零偏逐值一致，INS清除UNCALIBRATED；温稳降级仍独立存在。按用户要求保留新固件和EEPROM，未处理遥测。见 [复测记录](Validation/StaticThreshold-20260929/README.md)。

## 2026-09-29 持久化与静态校准板测

参数保存/复位加载、保存代次隔离、CRC与全片恢复通过；静态校准在确认静止后仍误报MOVING，未获得成功校准。8路1kHz订阅实收约7284标量/s，CDC停止读取/重连可恢复。Monitor约0.05%、临界区2µs；最终运行正常Minimal Release、原EEPROM恢复、探针释放。完整范围和未测项见 [本轮记录](Validation/PersistenceBoard-20260929/README.md)。

## 2026-09-28 单份持久化与高频变量

FT24C128A身份/双IMU校准/业务参数单份记录、Persistent与只读Telemetry、SOURCE V2已实现。25套主机、90公共头/156编译及依赖单元、十个父项目固件、命名/结构/第三方哈希、Debug/Release及独立板测入口通过。Release D1剩余14048 B；未烧录，真实断电/高频遥测/INS与CAN组合负载仍待验收。见 [本轮记录](Validation/Persistence-20260928/README.md)。

## 2026-09-28 CAN1/CAN3 吞吐与 ADC 仪表单点

修复真实板测发现的 CAN weak 通知空实现；INS 有效后双向合计 6000/8000 帧/s 均完整交付，CPU 76.77%/95.21%，过载出现明确提交背压。ADC 均值 24.641 V/0.08986 A，电压较用户 24 V 高 2.67%，不据单点改系数。恢复正常 Release 并释放探针，完整结果及未测边界见 [记录](Validation/CAN13-ADC-20260928/README.md)。

## 2026-09-28 SRNOS 当前固件烧录与板测

用户授权后烧录并校验新固件，完成两组统计开/关对照、1600 次生命周期/11384 检查零失败、ADC 四项软件注入、CDC 停读/DTR 重连。新增统计 CPU 0.99155/0.98988 个百分点（余量很小）、Monitor 约 0.0492%、监测临界区 2 µs；含预采集的完整 hook 峰值 0.762 µs。最终恢复正常 Minimal Release 并释放探针，独立一分钟双 IMU/INS 各 60001 条有效样本，五流缺序/协议错误为 0。温稳曾通过后退出，INS 有效但部分时间降级；CAN 实际负载、仪表精度、串口和物理故障未验收。见 [当前板测与原始数据索引](Validation/SRNOSBoard-20260928/README.md)、[底层后续清单](Development.md)。

## 2026-09-28 SRNOS 全面命名、公共隔离与运行优化

23 套主机测试、87 公共头/151 编译及依赖单元、十种父项目固件、13 条真实应用编译命令隔离、110 个 SDK 对象全局名、结构/第三方哈希及 Debug/Release 通过。落实事件等待、有界工作、活动请求去重、锁外回调、预留复制与 SDK 超时；保持线程分离和硬件时序。新增固定存储、D2/D3/TLS/heap 和 ELF 校验值见 [本轮记录](Validation/SRNOSBoundary-20260928/README.md)。此次未烧录，先前板测不替代本轮性能与负载验收。

## 2026-09-28 托管任务与 SystemMonitor 板端实测

用户授权烧录后完成生命周期、统计开关对照、ADC/故障注入、CDC 背压/DTR 重连。每组 400 次生命周期、2846 检查零失败；循环统计新增 CPU 0.5535 个百分点，完整 hook 最大 0.70 µs，Monitor 平均 0.05176%、最长临界区 2 µs。修复 BDMA 停止误隔离和 CLI SAMPLE 消息号。最终运行正常 Release、已释放调试器；仪表精度和外部 CAN 负载仍未验收。详见 [板端记录](Validation/ManagedTasksBoard-20260928/README.md)。

## 2026-09-28 SystemMonitor

SystemMonitor替代Diagnostics，ADC五参数1 Hz与逐槽线程统计、两条HostLink遥测及主机JSONL工具完成。22组主机套件、根Minimal Debug/Release、九个父项目固件、结构/回调/内存检查通过；ADC缓冲位于D3、32字节对齐。未烧录或板测，CPU≤0.1%/临界区≤10 µs及模拟精度仍待验证。详细记录见 [SystemMonitor-20260928](Validation/SystemMonitor-20260928/README.md)。

日期：2026-09-26。环境：macOS，ST Arm Clang **21.1.1 rel2.20260323-1806** / Picolibc，STM32CubeCLT **1.22.0**。前期记录为主机、源码与链接验证；后续用户提供的首次板端调试结果单独记录在文末。修复开发过程中未执行 CubeMX generate、烧录或操作硬件会话。

## 已通过

| 层次 | 结果与范围 |
|---|---|
| 来源与仓库边界 | 覆盖全部49个Framework目录；323个来源文件SHA-256复查一致；未修改prototype源码。SDK中无嵌套`.git`/`.gitmodules`/gitlink；`Tests/Build`未被忽略。 |
| 命名与构建 | 自维护源码、公开头、配置、工具和测试无旧基础命名引用；历史CHANGELOG和一次性迁移规则保留更名前字符串。所有`Source`实现均出现在显式编译清单中，不引用prototype构建路径。 |
| Core / Fault | C/C++基础操作、边界与负编译用例、Fault记录、Panic解码、libc arena边界测试通过。更名保留原Fault记录布局。 |
| 算法 | PID/Control、Math/Kalman、通用功率模型通过；Math和IMU组件使用固定版真实CMSIS头与实际C矩阵backend，避免内联替身掩盖链接缺失。 |
| CAN / IMU | 路由容量、过滤/发送队列、冻结后注册拒绝；INS、温控/Quaternion组件及全部7种方向预设通过。 |
| 配置 / Serial / 协议 | 资源冲突、协议格式、无owner拒绝、配置副本和冻结、总启动堆预算；真实Serial状态机的复制发送、DMA cursor/环绕、RX溢出、UART TC等待、事务互斥、超时清理；DR16/VT03/裁判坏帧恢复、H12H响应匹配、RS485 endpoint/CRC/广播/响应匹配通过。 |
| 启动临界区 | RemoteControl配置的PRIMASK保存恢复与冻结回归通过；配置阶段不依赖FreeRTOS临界区的启动哨兵计数。此检查不等同于默认固件曾经板端死锁。 |
| HostLink / 参数 | Codec、原HostComm/CONFIG协议、注册冻结、业务写入授权、类型/范围检查、原子batch/并发snapshot、持久化损坏不覆盖当前值通过。EEPROM参数起点0x40，IMU校准保留0x20–0x3F。 |
| USB | 双CDC分发隔离、DTR/断连、日志队列/回放主机测试通过。确认固定AL94 CDC class静态分配，没有EP0 IRQ调用libc分配器的路径。 |
| 固件矩阵 | 根Debug、根Release、Board独立Debug、Board独立Release编译链接通过。 |
| 父项目矩阵 | 含空格的外部父工程目录；helper与直接链接Platform两种接入；PrototypeBindings、Parameters、RawBinding；关闭GC的AllComponents，共6种目标通过。94个公共头在外部C++消费者中共同编译。 |
| 内存与容量 | ELF检查D1 data/BSS/TLS、RTOS两区heap、独立libc/MSP、D2 DMA/Watch/retained、D3 BDMA、ITCM、32字节对齐；父工程TLS/构造链接通过。MSP=12KiB、libc=4KiB覆盖生效；故意设置超容量libc时按链接断言拒绝。 |
| 异常与回调 | 检查真实向量表指向6个SDK异常入口，Clang反汇编首指令读取MSP/PSP，无捕获前C序言；HAL callback的源码所有者与全组件强符号链接通过。UART采用Board注册回调；生成DMA1 Stream0/1入口经构建改名，由Board私有handle接管。 |

12组主机套件统一入口：

```sh
python3 Tests/run_host_tests.py
python3 Tests/Build/check_structure.py --compile-commands build/Debug/compile_commands.json
```

交叉编译与父项目回归：

```sh
export PATH="/opt/ST/STM32CubeCLT_1.22.0/st-arm-clang/bin:$PATH"
cmake --preset Debug
cmake --build --preset Debug -j 8
cmake --preset Release
cmake --build --preset Release -j 8
python3 Tests/Build/check_sdk.py
python3 Tests/Core/check_foundation_elf.py build/Debug/SirinEmbedOS.elf
python3 Tests/Core/check_foundation_elf.py build/Release/SirinEmbedOS.elf
```

在`Board/`执行同名preset可验证板级独立入口，ELF在`Board/build/<preset>/sdk/SirinEmbedOS.elf`。

## 默认镜像资源

默认Minimal已装配CAN/IMU/日志/HostLink/USB/诊断等固定能力，普通UART未绑定。以下为本轮最终链接输出，不是运行期峰值或应用可用预算保证。

| 区域 | Debug | Release |
|---|---:|---:|
| Flash | 242,328 B | 171,648 B |
| D1（含128KiB RTOS heap、8KiB libc） | 269,472 B / 320KiB | 269,600 B / 320KiB |
| D2 DMA/Watch/retained | 9,952 B / 32KiB | 9,952 B / 32KiB |
| D3 BDMA | 512 B / 16KiB | 512 B / 16KiB |
| ITCM | 224 B / 64KiB | 192 B / 64KiB |

DTCM中RTOS heap为65,536 B、热数据为41,568 B、顶部MSP为8,192 B。链接器因顶部显式保留MSP显示128KiB/100%，其中含地址空洞；不能将该百分比误解为当前线程已耗尽全部DTCM。实际边界由链接断言和ELF检查验证。普通heap不保证DMA可达。

## 修正过的集成问题

- I2C/SPI旧feature宏使DMA段失效：改用集中内存开关；I2C4 staging已确认位于D3而非不可达D1。
- Watch旧扁平头路径使专用段失效：改用公共集中内存合同。
- Cortex-M7/Picolibc允许未对齐访问，默认不强开UNALIGN_TRP；基础字节API保留安全实现。显式开启用于审计时另行验证库行为。
- 全组件链接暴露`arm_mat_init_f32`缺实现：补同语义弱backend，并用真实CMSIS头测试。
- 日志、遥控配置和USB首次初始化的启动/并发边界已整理；参数恢复先于USB开放，应用声明线程等待启动屏障。

## 仍需板端验证

以下为完整验收项目，均 **尚未验收通过**；首次启动的局部观测见文末，不替代持续运行和压力验证：

- 调度首次启动、各任务栈高水位、FPU上下文、每任务TLS/errno隔离，以及malloc/stdio并发。
- SRAM/MPU/cache属性、NOLOAD/retained跨复位行为，Fault触发、解码与显式紧急输出。
- 共享SPI4高负载下BMI088与MCP2518FD并行工作、优先级/超时/恢复；三路片内CAN及外置CAN收发与Bus-Off恢复。
- IMU采样、温控、校准与EEPROM实际分区读写；物理方向和55°C目标需要按当前板确认。
- UART实际极性、921600等速率、持续DMA跨圈、不同优先级IRQ嵌套、长临界区下丢字节统计、TC/错误恢复。
- UART7 RS485的DE/RE波形、静默间隔与多设备请求—应答；H12H实际适配器接线和时序。
- 双CDC枚举、描述符字符串、DTR、断连重连、日志/HostLink同时压力、主机存储命令。
- 板载LED/蜂鸣器的DMA1 Stream0/1接管与定时器实际时钟；固定硬件服务的健康状态。

当前未增加FatFs/LwIP/MSC/DFU。CMSIS原有GCC `optimize("Os")`属性在Clang下仍有忽略警告，未修改上游头。编译通过不能替代上述运行验证。

## BringUp 后续开发验证（同日）

- `Examples/BringUp` 独立 Debug 构建通过，ELF 基础内存/TLS/libc/保留段及异常向量检查通过。Flash 为 244,848 B，D1 为 276,384 B；与默认 Minimal 相比增加 2,520 B Flash 和 6,912 B D1，包含有界静态观测缓冲。
- 根 Debug 重新配置及构建通过，缓存仍选择 Minimal，资源占用与上表一致。
- `Tests/Build/check_sdk.py` 全部通过：原六种父项目目标加 `BringUpExample`，共七种；继续覆盖 MSP/libc 容量参数和故意越界拒绝。
- `Tests/Build/check_structure.py` 通过：301 个源/头/工具文件合同，27 个唯一 HAL callback 源码所有者，49 个模块和显式源码覆盖。
- 本次未改 SDK 服务或协议实现，未重复运行 12 组主机套件。上述新增检查均为编译/静态验证，没有硬件结果。

构建、调试符号解释与扫描扰动限制见 `../Examples/BringUp/README.md`，后续验收顺序见 `Development.md`。

## 首次板端观测与启动屏障修复（同日）

来源：用户使用 J-Link V8.96 + ARM GDB 获取的 `/tmp/sirinos-bringup-gdb-20260926-readable.log`。用户已确认板上向量表/代码与 BringUp ELF 匹配。本次阅读记录复核到：

- 系统进入 `SRNOS_SYSTEM_RUNNING`；原生 CAN 初始化、USB 启动状态为 true，完成一次 BringUp 采样，内部 IMU 数据新鲜。这不代表 CAN 实际收发或 USB 枚举已验证。
- 早启动 CAN worker 等待 `0x2` 时返回 `0x2`；Indicators 在 HAL tick 464 时等待同一位，返回 `0x3`，因为应用就绪位 `0x1` 也已置位。
- 旧 `kernel.c` 第 83 行的等值断言错误地拒绝 `0x3`，进入 Fault/Panic 并形成复位循环。应用声明线程等待 `0x1` 的等值断言存在同类问题。
- 当次记录 heap_5 当前/历史最低空闲均为 116,416 B，只是失效前局部观测，不是稳定运行预算。

修复：两处共用私有 `SRNOS_StartupWaitReady()`，返回条件为无 `osFlagsError` 且 `(flags & required) == required`。保留全部就绪位供晚调度线程使用。已检查其他 IO 事件等待，未发现相同的等值断言。

新增 `Tests/Kernel` 使用上游 CMSIS 头和模拟等待返回值验证真实共享 helper：单位置位、`0x3` 合并就绪、缺位、零返回及 CMSIS 错误值；检查 WaitAll/NoClear/无限等待和仅调用一次。修复后的硬件持续运行、声明应用线程和复位恢复仍待验证。

本轮验证通过：13 组主机套件；304 个源/头/工具结构合同；修复版 BringUp Debug/Release 构建及基础 ELF 检查。Debug Flash 244,752 B、D1 276,384 B；Release Flash 172,872 B、D1 276,512 B。既有 CMSIS optimize 属性警告仍在。

调试记录 SHA-256：`e0062f2347ea2f4ed0f7367a8baf7af0d46615af65599de2d10df74e542c0a94`。保留的旧 `build/BringUp/SirinEmbedOS.elf` SHA-256：`f26c10d30282ebc92112d978840b49fbe327b2650d41842a7ac87d046087e0c9`。

构建采用独立目录 `build/BringUp-barrier-fix`（Debug）及 `build/BringUp-barrier-fix-release`（Release）。未覆盖旧 BringUp ELF，未连接、继续、复位或终止用户当前暂停的调试会话。源文件已更新，旧会话仍须按旧 ELF 的符号/行号解释，不能把新源码行号当作旧固件位置。


## 修复版连接与复位复测（2026-09-26，后续授权）

用户明确授权连接、复位及按需编译烧录。本轮先确认 CLion MCP 无活动会话、无其他 GDB/J-Link 进程，再使用 J-Link V8.96、USB 探针 10114514、STM32H723VG、SWD 4000 kHz、2331 端口连接。目标电压 3.30 V，GDB 为 `/opt/homebrew/bin/arm-none-eabi-gdb -nx`。

板上 `.isr_vector` 和 `.text` 与 `build/BringUp-barrier-fix/SirinEmbedOS.elf` 均 matched；ELF SHA-256 为 `43bf489614a3b52e8de74b3ea3681d43b9240a2fdb798a0710e6daf4ea006a75`。不据此宣称全部镜像段或运行数据一致。本次未重新编译或烧录，执行了一次 `monitor reset` 软件复位。

| 时点 | HAL tick / 采样次数 | 结果 |
|---|---|---|
| 初次 attach | 633,776 ms / 127 | 处于 Idle，RUNNING；完整快照中 IMU recent=true、年龄 0 ms。 |
| 继续运行后 | 648,057 ms / 130 | 心跳推进，当前 Fault active=0；KernelFailure 硬件断点未命中。 |
| 软件复位后 | 40,256 ms / 8 | RUNNING、IMU_APP_STATE_RUNNING、recent=true；Fault active/CFSR/HFSR 均为零。 |
| 再继续运行 | 98,835 ms / 20 | 完整快照、IMU recent=true、Fault active=0；未命中断言断点。 |

其他观测：

- heap_5 当前/历史最低空闲均 116,416 B；完整任务快照 19 个任务，Indicators 已越过启动屏障。
- CAN1–3 RX 的历史最小空闲栈各 412 B。源码有 32 个 `SRNOS_FDCANRxFrame` 的局部数组，目标 sizeof 为 108 B，即 3,456 B；尚无溢出证据，但真实 CAN 负载下栈余量需优先复核。
- 复位后约 40 秒，MCP SPI xfer_calls/xfer_ok 均 15,958，busy/timeout/error 均零；外置 CAN init_errors=0、rx_frames/tx_frames 均零。仅证明此窗口内初始化及 SPI 访问，不证明 CAN 帧收发。
- USB ready=true，但日志 usb_configured=0、tx_bytes=0；CDC 枚举和端到端日志未验证，不能仅据此断定 USB 实现故障。
- GDB 栈展开出现相邻帧相同提示，未将其单独当作真实递归或栈损坏证据。

结论：本轮持续观察及一次软件复位均未复现旧启动屏障断言。暂停、断点和 BringUp 栈扫描会扰动时序；未完成实时负载、CAN/UART/USB 压力或整板验收，软件复位也不替代断电上电。

结束时已删除本次唯一新增的 KernelFailure 硬件断点，断点列表为空，执行 continue 让 MCU 继续运行。GDB/J-Link 会话保留并继续占用探针；未 detach 或关闭服务器。日志为 `/tmp/srnos-barrier-live-20260926.log`，后续会话活动可能继续追加。


## 2613 传感器与 SPI4 调度（2026-09-26）

实现与接口见 [Sensors.md](Sensors.md)，运行入口见 [SensorBringUp](../Examples/SensorBringUp/README.md)。本轮经用户授权编译、烧录并复用唯一 ARM GDB/J-Link 会话；没有主动发送外部 CAN 测试帧，没有访问新 EEPROM，也未改 ADC/PHY 或生成代码。验证目标是电气可达性、驱动和局部共享总线运行；LSM 尚未达到可达性验收。

### 主机和固件

- 原 13 组加 SPI、I²C、Sensors，共 16 组主机套件通过。SPI 覆盖两 IMU/持续普通事务、公平性、槽位上限、CS 唯一性、TX 复制、RX 生命周期、取消/回调所有权、队列/活动超时及迟到回调；I²C 覆盖设备完成归属、7 位地址、确认 abort 及失败隔离。
- Sensors 覆盖 ID/版本拒绝、初始化超时、未就绪、大小端/符号/单位、系数补偿、失效/恢复、快照过期及 BMI 每周期发布与发起。RAW worker 测试不链接融合、加热、校准、EEPROM 符号。
- 根 Minimal Debug/Release、Board 独立 Debug、SensorBringUp Debug/Release、父项目八种目标通过；95 个公共头、MSP/libc 覆盖与越界拒绝、第三方哈希、异常向量/TLS/内存检查通过。
- 结构检查：327 个源/头/工具合同、28 个唯一 HAL callback、49 个迁移模块、显式源码覆盖及无嵌套 Git 元数据。
- 最终 SensorBringUp Debug ELF 的 SPI 队列位于 D1（bus=0x240072d8），SPI RX/TX staging 为 0x300000c0/0x300002c0、各 512 B；I²C TX/RX staging 为 0x38000000/0x38000100、各 256 B。D2/BDMA 可达性及对齐已加入 ELF 回归。D1 使用 277,824 B/320 KiB，D2 9,952 B，D3 512 B，ITCM 224 B；DTCM 显示 100% 包含 heap/MSP 预留，不等于运行期耗尽。

主机日志 `/tmp/srnos-final-host-tests.log`，父项目日志 `/tmp/srnos-sensor-parent-final.log`。最终 ELF 备份 `/tmp/srnos-sensors-final-20260926.elf`，SHA-256：

`82db9321fb8f9d65e36f47e28345d1a52088dba3a6a93af2ece501abb3485be9`

### LSM 软件排查

LIS2MDL WHO_AM_I=0x40，SPA06 ID=0x11（产品 1/版本 1）；BMI/MCP 在相同 SPI4 正常推进。LSM WHO_AM_I 始终为 0x00，已分别测试 mode 0、按手册的 mode 3 及正确 SCK 空闲电平，并临时降低 SPI 时钟至 625 kHz，仍无有效 ID。临时启用 PE5 MISO 内部上拉后，读取变为 0xFF。

这支持“选中 LSM 时没有有效器件驱动 MISO”的判断，不能区分未焊接、未供电、焊接/布线或 CS/SDO 连接问题。用户表示不确定器件装配情况。最终复位恢复正常 MISO 配置和 10 MHz，驱动保持 OFFLINE，每秒重读 ID；未对未知器件盲写初始化寄存器。下一步需要结合实物检查供电、PD5/CS、PE5/SDO 和器件装配；软件尚不能证明具体硬件故障位置。

### 测量方法与限制

最终镜像烧录后先采集完整 RAM 快照与任务栈，再持续运行至少 10 分钟；窗口中不暂停、不进行全任务栈扫描。GDB 的 KernelFailure、LIB_INS_Init、IMU_CalibStorage_Init、SRNOS_ParametersLoad 硬件断点用于确认没有进入这些路径。栈统计在窗口前后暂停，由主机导出 RAM 并计算填充水位，不在目标调用函数。

状态和计数来自每秒发布且 sampling=false 的快照及暂停后读取的总线计数；两者时刻有小幅差异，不混算完成率。样本率使用固件完整 10 秒窗口，最大周期包含启动过程，不能把最大值当稳态周期。USB 仅验证启动状态，未确认 CDC 枚举/日志到主机；RAM 快照是本轮主要观测渠道。

### 连续窗口结果

前后 HAL tick 为 41,579 / 664,995 ms，连续运行约 623.4 秒；完整发布快照 tick 为 41,014 / 664,015 ms，sequence 为 42 / 665，均 sampling=false。窗口期间未触发上述断点；Fault active、CFSR、HFSR 均为零。最终板上 `.isr_vector`、`.text` 与上述独立 ELF 备份 matched。

| 器件 | ID / 状态 | 窗口前后有效样本计数 | 末次 10 秒样本率 | 末次轮询率 |
|---|---|---:|---:|---:|
| BMI088 | 0x1E / READY | 40,582 → 663,582 | 1000 Hz | 1000 Hz |
| LSM6DS3TR-C | 0x00 / OFFLINE | 0 → 0 | 0 Hz | 1000 Hz |
| LIS2MDL | 0x40 / READY | 3,792 → 63,152 | 93.0 Hz | 100 Hz |
| SPA06-003 | 0x11 / READY | 668 → 10,839 | 16.4 Hz | 100 Hz |

末次窗口时长 10,000,000 µs，样本数 10000/0/930/164，轮询数 10000/10000/1000/1000。LIS 在这段较长窗口的平均更新率约 95.28 Hz，实测不等于标称 100 Hz；SPA 平均约 16.33 Hz。BMI 完成率达到本轮 ≥990 Hz 目标，但 LSM 未识别，**双 IMU 的板端目标未通过**。

SPI 完整快照中 ACC/GYRO/MCP/LSM 接纳与完成分别为 1,327,179 / 663,596 / 265,640 / 664，均逐项相等；队列满、排队过期、传输错误、超时及恢复均为零。最大排队时间 20/6/15/6 µs，最大传输时间 19/19/18/12 µs。暂停时实时 SPI start=done=2,260,412；MCP calls=ok=266,032。I²C start=270,354、done=270,353，暂停恰在一笔磁场读取中；错误/超时/恢复均零，单笔在途不解释为泄漏。

BMI 最大采集耗时 85 µs。LSM 的 664 次驱动 errors 对应 664 次无效 ID 探测，不是 SPI DMA 错误；离线重试未阻止其他设备。有效设备均只有一次初始化，没有恢复或采样超期。

末次 BMI 加速度约 [-1.068, 0.049, -9.683] m/s²，模长约 9.74 m/s²；磁场 [-21.15, -35.10, -15.00] µT；压力 84,188.6 Pa、温度 53.92°C。数值有限并持续变化，但没有外部温压参考和受控转动，不能据此宣称绝对精度、安装方向、角速度响应或校准通过。

heap 当前/历史最低空闲均保持 121,872 B。18 个任务的填充栈水位前后相同：IMU 1,396 B、LSM 1,572 B、Environment 1,456 B、SPI dispatcher 2,816 B、I²C callback 2,908 B、MCP RX/TX 3,620/3,676 B。片内 CAN RX 仍各 412 B，真实帧压力下需优先复核，不能把无负载余量当作满负载保证。

检查工具还修复了一项取证问题：`starm-objcopy --dump-section` 未指定输出 ELF 时会重写输入容器。现显式写入临时目录；独立烧录 ELF 的 SHA 保持不变，构建产物七个 Flash 段与它完全相同。后续使用独立备份核对板上镜像，不依赖被旧检查工具重写后的整个文件 SHA。

### 软件复位与结束状态

连续窗口后执行一次 `monitor reset`。约 57 秒后重新暂停：HAL tick=57,437，完整快照 tick=57,014、sequence=58、sampling=false、RUNNING。BMI/LIS/SPA 为 READY，累计样本 56,582/5,292/930；LSM OFFLINE、58 次探测、无样本。末次 10 秒有效率为 BMI 1000 Hz、LIS 92.4 Hz、SPA 16.4 Hz。SPI start=done=194,107，I²C start=done=23,092，MCP calls=ok=23,008，错误/超时/恢复、Fault active/CFSR/HFSR 均零，heap 余量保持 121,872 B。SPI CFG1=0x20070007，恢复正式 10 MHz 配置。

复位期间未命中 KernelFailure、融合初始化、IMU 校准存储初始化或参数加载断点。该证据结合 RAW 分支和主机测试证明本验证配置绕过这些路径；没有把未访问 EEPROM 解释为新 EEPROM 驱动已适配。

本次四个断点（13–16）已删除，列表为空，执行 continue 后 **MCU 运行中**。GDB/J-Link 保留并继续独占探针，未 detach/关闭服务器。完整开发调试日志 `/tmp/srnos-sensors-2613-20260926.log` 包含多次中间固件；归档的 [最终固件会话](Validation/2613-20260926/final-session.txt)、[计数与配置](Validation/2613-20260926/summary.json)、[窗口前栈余量](Validation/2613-20260926/stacks-before.json)、[窗口后栈余量](Validation/2613-20260926/stacks-after.json) 仅对应最终镜像。原始 RAM 转储在 `/tmp/srnos-sensors-final-{before,after}-{dtcm,d1}.bin`。

本阶段软件实现和可运行器件的局部验证已完成；**LSM 识别、配置及持续采样、双 IMU 1 kHz 板端验收仍未通过**，需硬件可达后继续。实际 CAN 帧压力、断线/abort 故障恢复、USB 枚举、传感器轴向/精度/受控响应仍未测。不扩大为整板验收，也没有自动提交、推送或操作 prototype。


## 2026-09-27：HostLink 双 IMU 遥测与 BringUp 指示短测

本条覆盖较早的 LSM OFFLINE 结论。最终配置为 SensorBringUp：BMI RAW、LSM/LIS/SPA 开启；BMI/LSM 1 ms；融合、用户校准、加热、参数加载、HostLink LOAD/SAVE 和周期栈扫描关闭；MCP 只被动运行。用户要求十分钟自行执行，本轮改为短测，**未完成十分钟稳定性验收**。

### 固件、主机测试与构建

- 最终烧录 ELF：`build/SensorBringUp/SirinEmbedOS.elf`，取证副本 `/tmp/srnos-telemetry-final-20260927.elf`；SHA256 `2574a80f1c9e501db5520fc2867078048c7710fc3d1df618c62fec7d8ca83d4e`。使用 J-Link V8.96、探针 10114514、SWD 4 MHz、STM32H723VG、目标约 3.30 V；向量表及 .text 与该 ELF 匹配。
- 18 组主机套件通过，增加/扩展 COBS 坏帧与超长恢复、槽位复用后的响应重发、FIFO 次序/容量/命令余量、发送复制与会话 epoch、DTR 下降仍持有 TX buffer、endpoint 关闭取消、订阅周期校验、storage gate、观察回调、64 样本队列、状态/编码与 CLI 黄金向量、启动音/黄色提交/心跳边沿。
- 根 Minimal Debug/Release、BringUp Debug、SensorBringUp Debug/Release、Board Debug、父项目八个固件目标（含全部组件、C++ 公共头/TLS）及容量/第三方哈希/IRQ/heap 检查通过；结构检查 345 个源码/头/工具、28 个唯一 HAL callbacks、49 项迁移来源通过。
- SensorBringUp：FLASH 252736 B；D1 289696/327680 B（88.41%）、D2 9952 B、D3 512 B、ITCM 224 B。两颗 IMU 队列与编码缓冲在 D1，DMA/BDMA staging 仍在 D2/D3；不新增线程、不更改 heap/MSP/libc 分区。基础 ELF 内存/TLS/异常向量另经静态脚本检查。

### 实板短测

CDC1 `/dev/cu.usbmodem355C343932333`，CLI Python 3 + pyserial。握手后未订阅期间无样本事件；订阅后记录如下，数据来自不暂停调试器的窗口：

| 窗口 | 时长 s | BMI 采集/s | BMI 接收/s | LSM 采集/s | LSM 接收/s |
|---|---:|---:|---:|---:|---:|
| 正常 1 | 10.0040 | 1000.001 | 1000.001 | 1000.001 | 999.601 |
| 正常 2 | 10.0032 | 999.984 | 1000.084 | 999.984 | 999.684 |
| 软件复位后 | 10.0047 | 999.928 | 999.928 | 999.928 | 1000.028 |

正常窗口均无序号缺口/重复、CRC/COBS 错误、设备 overflow/failed。包边界与状态查询使窗口接收数和采集数可相差几条，不是丢样。所有 XYZ 原始值、物理值、原始温度及 °C 均已解码；例如 BMI 约 42°C、LSM 约 45°C，仅表示读到有限数据，不代表完成精度验证。

- 暂停主机读取 3 秒后恢复正常，无协议错误。这次由主机缓冲吸收，设备 overflow 仍为 0，**没有触发实板满队列**；满队列丢新、计数与恢复由主机注入测试覆盖。
- 取消订阅后缓存深度为 0、样本事件停止；DTR 下降/上升后 HELLO 成功，订阅已清除，没有旧会话样本，重新订阅恢复约 1 kHz。主动清理的尾部样本计入 discarded。软件复位重新打开 CDC1，HELLO/订阅与正常窗口均通过。
- 短测前后 MCP 完成事务从 78440 到 106040，queue_full/expired/errors/timeouts/recoveries 均 0；SPI/I²C 无传输错误，Fault active/CFSR/HFSR 均 0。四颗器件 READY；LSM ID 0x6A、LIS 0x40、SPA 0x11。
- `srnos_indicators` 记录 prepared=1、boot_commits=1、boot_notes=3、errors=0，heartbeat_edges 随时间增长；软件复位后重新出现同样的一次性启动计数。TIM3 CCER=0，加热控制器 ready=0。实际灯色、亮度和声音无人机旁确认，不记为感官验收通过。
- 窗口前后暂停、导出 RAM 在主机计算栈水位，不在采样窗口内执行全任务扫描。HostComm 最小空闲栈从 1492 B 到 560 B；HostCmd 1436 B、IMU 1396 B、LSM 1572 B、Indicators 884 B，free_heap/minimum_heap 均 115648 B。CAN1/2/3 RX 最小余量仍约 408 B，为既有待压力验证项；本轮没有提高负载或修改这些栈预算。

证据保存在 [telemetry-20260927](Validation/telemetry-20260927/)：`short-check.jsonl`、`reset-cli.log`、`gdb-final.log`、前后栈水位和主机测试记录。完整本地 GDB 日志 `/tmp/srnos-telemetry-gdb-20260927.log` 包含先前试版，不能混用其镜像和计数。

结束状态：MCU **运行**，无断点，GDB/J-Link 会话保留占用探针；CLI 已取消订阅并关闭 CDC1，可由用户运行 [CLI 长测](HostLink.md)。没有提交或推送。十分钟稳定性、真正 USB 满队列实板实验、USB 拔插、普通 BringUp legacy 故障恢复的实板加热门控、实际声光及满负载 CAN 仍未完成。新功能的短测通过不等同于整板验收。

## 2026-09-27：RAW BMI 60°C 加热与易读 CLI

- SensorBringUp 显式设置冻结配置 `raw_heater_target_c=60`；默认 RAW 仍为 0（关闭），legacy `heater_enabled` 行为不变。原始采样不经过融合、用户校准和 EEPROM。BMI 新鲜温度驱动已有 PWM/PID，无新增线程；超过 10 ms 无温度、时间倒退/重复、非有限温度或达到 70°C 锁存关闭，PWM 启动失败也锁存故障。
- Sensors（含 RAW heater）、IMU、Serial/SystemConfig、IMUTelemetry/CLI 主机套件通过。覆盖显式启用、1 kHz feed、过期/无效/超温锁存、配置范围、温度与旧值显示。SensorBringUp Debug/Release、Minimal Debug 构建通过；foundation ELF 检查通过。D1 Debug 289728 B，D2 9952 B，D3 512 B；未修改 IOC、DMA 布局或第三方版本。
- 本机 J-Link V8.96，探针 10114514，SWD 4000 kHz、3.30 V。旧服务连接已失效，第一次下载无法校验；关闭旧服务后重建唯一服务并重新下载，`.isr_vector` / `.text` 均匹配。固件 `/tmp/srnos-heater60-20260927.elf`，SHA256 `9da115c769506450c92f5fbf61cc6f8abf8a398c58a5d3ced0df83f06466f4a3`。
- 15 秒 monitor 与 5 秒 capture 均完成，CLI 默认显示双温度/接收率/缺序/设备溢出，capture 保存格式不变。短采集 `/tmp/srnos-heater60-capture-20260927`：BMI 4996、LSM 4995 正式样本；按设备时间分别 1000.0006、1000.0002 Hz，无缺序、重复、无效、时间回退、协议错误或设备溢出/发送失败。正式窗口边界外样本另按 stopping 分类，不将行数差异当作丢包。
- 5 秒窗口 BMI 温度 59.625–60.125°C，LSM 63.21875–63.8984375°C。GDB 一次快照：目标 60°C，controller Stable，enabled=true、fault_latched=false，BMI 当前 60°C，TIM3 CCR4/ARR=6000/10000。只证明短时加热起效，不宣称长期温稳或 PID 调优完成；两芯片温度不相等。
- 证据日志 `/tmp/srnos-heater60-gdb-20260927.log`、`/tmp/srnos-heater60-monitor.log`、`/tmp/srnos-heater60-capture.log`。取证后已软件复位并继续运行；无断点，保留 GDB/J-Link 唯一会话，CDC1 由 CLI 正常取消订阅并释放。任务内温度失效保护不能覆盖调试暂停/CPU 停顿；测量时勿暂停 MCU。十分钟采集仍由用户执行。

## 2026-09-27：LSM 优先温控与连续 PI

- RAW 温控优先读取健康、新鲜的 LSM 快照；无效/离线/过期回退 BMI。BMI 正常时，LSM 恢复连续健康100 ms才切回。固定恢复策略不开放运行时改绑；同一60°C目标应用于实际来源，无人为温差换算。切换清积分、PWM归零后按斜率缓升。两路都失效后锁存关闭；任一有效来源达到70°C也关断。BMI阻塞重初始化前先暂停输出。
- RAW控制采用位置式连续PI：Kp40、Ki2、Kd0，输出500、积分350，升/降斜率25单位/秒，即5个百分点/秒。取消旧RAW的全功率预热及越过目标立即清积分；legacy模式保持原有策略。新增公开只读 `SRNOS_SensorGetHeaterStatus` 一致快照，CDC0每秒记录实际来源、温度、目标、占空比、故障。
- Sensors/RAW heater、IMU温控、Serial/SystemConfig、IMUTelemetry/CLI套件通过。覆盖回退/恢复防抖、源切换归零、非反馈源超温、两路失效、初始化暂停、连续PI斜率/跨目标不清积分；并覆盖SPI高优先级回调在温度快照复制中发布时不误判未来样本。SensorBringUp Debug/Release与Minimal Debug构建、foundation ELF检查通过，无IOC/调度线程/DMA布局变更。
- 初版30秒实板验证（SHA `b0a1e21efc1c197b8791f2ecc18a378354a27f68a280af6a3fe1d6bd9da84c4f`）：两路正式29972条，按设备时间BMI999.99993、LSM1000.00000 Hz；无缺序、重复、无效、协议错误或设备溢出/发送失败。CDC0持续 `src=LSM target=60.0 fault=0`，功率平滑变化；短窗口LSM尚约58.6–59°C，未证明最终稳态或6.2秒振荡消失。数据 `/tmp/srnos-lsm-heater-smoke-20260927`，日志 `/tmp/srnos-lsm-heater-cdc0.log`。
- 最终版本补强状态快照原子发布及快照读取时间边界后重新构建/烧录，ELF `/tmp/srnos-lsm-heater-final2-20260927.elf`，SHA256 `7ad66e160e895b0eac3a012fd749ac29720c3115099eb91d68e68ca25dcadcaf`。J-Link V8.96，探针10114514，SWD4000；向量表/代码段均校验匹配，下载日志 `/tmp/srnos-lsm-heater-gdb-20260927.log`。最终短时来源日志 `/tmp/srnos-lsm-heater-final2-cdc0.log`。
- 最终MCU复位后运行，无新增断点，GDB/J-Link保留单一会话，CDC端口由脚本退出释放。真实拔除LSM的故障注入未做，回退逻辑由主机测试验证；长期温度纹波/热响应及LSM低频变化改善仍需后续采集。未自动提交或推送。

## 2026-09-28：正式 INS、统一温控与五流遥测

正式服务取代旧单 BMI 融合、方向预设、旧温控、用户校准/IMU存储 worker；不提供旧模式选择。默认四颗传感器采样，固定板内旋转后经冻结的板到机器人四元数进入一个 Mahony 状态；磁力计/气压计只发布测量。默认60°C加热，LSM反馈优先/BMI回退。参数加载与HostLink LOAD/SAVE默认关闭。本轮没有适配EEPROM、执行用户标定、生成CubeMX代码、提交或推送。

### 主机注入与构建

- 19组主机套件通过。新增/扩展固定三组旋转与右手性、六面静置、业务安装旋转、变dt整圈运动与四元数符号、加速度权重、双/单路和恢复渐入、时间差选择、重复/预测/超期、3秒启动失败、60秒温稳降级、低动态/收敛、统一PI/全功率衔接、80°C保护与75°C下5秒恢复、触发器件离线禁止解除、LSM反馈恢复延时、温度/六轴无效关断及硬故障。
- 实际快照实现以IRQ边界模拟mutex，1个发布者与4个读者并发验证无撕裂、独立读取与超时失效。此为主机并发模型，不冒充实机ISR压力。
- 原双IMU协议与CSV回归通过；新增姿态/磁场/气压的C/Python共享黄金向量、订阅后新数据、固定容量/溢出、背压/清理/完成计数、状态和CLI解码测试。原HostComm会话/取消/断连/缓冲生命周期仍由既有套件覆盖。
- 根Minimal Debug/Release、BringUp Debug、SensorBringUp Debug、INSBringUp Debug/Release、Board Debug构建通过；父项目九个固件目标、完整公共头、C++/TLS、容量覆盖/拒绝溢出、第三方哈希和链接布局检查通过。345个源码/头/工具合同、28个唯一HAL callback、49项迁移来源通过。
- INSBringUp Debug：FLASH 250360 B；D1总保留300992/327680 B，D2 9696 B、D3 512 B、ITCM 224 B。未移动DMA/BDMA或heap/MSP/libc分区；新增INS High5/3KiB worker和静态遥测缓存计入预算。

### 实板过程与修正

工具 J-Link V8.96 + ARM GDB，探针10114514、STM32H723VG、SWD4MHz、3.30V。操作前没有其他GDB/J-Link会话占用。向量表与代码段在下载后及窗口结束均与取证ELF匹配。

初版低动态使用三轴残差向量范数RMS：实板合成角速度约0.65..0.69°/s，超过0.5°/s而正确停在观测。已明确采用**三轴分量整体 RMS `sqrt((varX+varY+varZ)/3)`**，约0.39°/s；仍保持0.5°/s门限，无输入滤波、无自动零偏、无跳过收敛。该口径已写入合同和回归测试。

初版BMI到下一周期才发布，造成双路时间差经常超过1ms，虽姿态1kHz但大量单路。修正为DMA完成通知原BMI任务，当周期取样发布；不增加线程、不在SPI回调中阻塞。最终两路最大时间差76µs，测量窗内全部双路等权。早期结果保留在 `preliminary-skew-check.jsonl`，不混入最终验收。

最终烧录ELF `/tmp/srnos-ins-accepted-20260928.elf`，SHA256 `393adbee9f41af5a7d4d1bcd0b4166310cc6006fa5adc706de642562124965ad`。随后只清除未使用的BMI校准宏/变量和更新注释，重编ELF的调试信息变化；两份完整Flash二进制逐字节一致，SHA256均 `ece4b11571cb4130e8444be24af6286d314d6005608a6745b5e265c9de3ea7e0`。取证副本未被覆盖，两份ELF SHA保存在 [summary.json](Validation/ins-20260928/summary.json)。

采集前先读取栈水位，再执行一次软件复位。复位后在新会话验证未订阅无样本、温稳等待60秒超时后低动态观测/收敛，再进入有效发布；之后温度到60°C附近并满足连续5秒温稳。始终保留温稳等待曾超时标志，达到温稳后THERMAL_PENDING清除。Application/通信在INS未就绪时已可使用。

### 三个不暂停的10秒窗口

五条流同时订阅：原始BMI/LSM、姿态、磁场、气压。姿态计算率用设备微秒及publications差分，主机接收率用主机单调窗口；包边界/状态查询可造成少量窗口计数偏移。

| 窗口 | 姿态计算 Hz | 姿态接收 Hz | BMI接收 Hz | LSM接收 Hz | 磁场接收 Hz | 气压接收 Hz |
|---|---:|---:|---:|---:|---:|---:|
| 1 | 1000.000 | 1000.403 | 1000.103 | 1000.103 | 91.764 | 16.294 |
| 2 | 1000.000 | 1000.027 | 999.627 | 1000.127 | 91.766 | 16.394 |
| 3 | 1000.000 | 1000.172 | 999.773 | 999.773 | 92.579 | 16.296 |

全部序号缺口、重复、CRC/COBS错误、设备overflow/failed为0；双路权重均0.5，窗口内无单路或预测。LIS实际更新率不等于标称100Hz，没有补帧。取消后不再收到样本且缓存清空；DTR下降/上升清理订阅，HELLO及重新订阅姿态后1秒收到1000条，无协议错误。原始CSV兼容由主机测试确认，本次未重新执行十分钟保存采集。

窗口后完整调试副本：姿态累计73488次、双路73488、单路/预测0；最大服务执行41µs（含有界遥测复制）、快照临界区最大6µs、最大周期1056µs、missed_deadlines=0；四颗传感器READY。板静置数据在转换后两个加速度Z均正，约9.7m/s²；只验证此静置符号，不宣称实板六面或受控三轴动态已经完成。

末次温度LSM60.14°C、BMI56.5°C，反馈为LSM、目标60°C、温稳true、PWM约15.8%，无过温/硬故障。MCP完成54440次、SPI start/done=736718/736718，错误/超时/恢复均0；I²C有一笔在途，错误/超时/恢复均0。Fault active/CFSR/HFSR均0。MCP没有主动发送外部CAN测试帧，不能据此宣称实际CAN负载压力通过。

自由/历史最低heap均112448 B。后测栈填充余量：INS2336 B、BMI1572 B、LSM1572 B、SPI dispatcher2640 B、Environment1456 B、HostComm484 B、HostCmd1436 B、Application3288 B。CAN1..3 RX约400 B，沿用的CAN栈及HostComm在更大真实负载/自定义handler下仍需复核。窗口内没有全任务栈扫描，前后数据见归档JSON；前测之后执行了软件复位，前后不是同一次运行的累计水位。

指示器prepared=1、boot_commits=1、boot_notes=3、heartbeat_edges=273、errors=0；实际声音、灯色和亮度没有现场感官确认。80°C/掉线/硬故障恢复由主机注入覆盖，未把实板加热到保护温度，也未物理断开传感器。

### 证据与结束状态

证据目录 [ins-20260928](Validation/ins-20260928/) 保存主机套件/构建结果、全部窗口/启动状态、最终易读CLI输出、GDB会话、前后栈水位、配置和固件SHA。GDB日志包含中间镜像，最终证据以SHA和summary指定范围为准。

结束时MCU**运行**、断点列表为空；GDB/J-Link保留并占用探针，CDC1已取消订阅并关闭。恢复运行后的易读CLI再次显示双路等权、约1kHz和60°C温稳。十分钟长测由用户执行；实板单路断开/恢复、动态精度、用户校准、磁航向、实际CAN压力、硬件热保护和完整机器人控制闭环均不在本次通过范围。


## 2026-09-28 FT24C128A 器件驱动与整片备份恢复

独立 `SRNOS_EEPROM*` 阻塞/非阻塞状态机已完成：16 KiB、两字节地址、分页写和读回校验、分段读、超时/设备级取消。20 组主机套件及专用 Debug/Release 构建通过；板端 20 项检查通过，16 KiB 恢复数据与外部备份逐字节一致，测试前完整 Flash 已恢复并验证一致。详细 SHA、资源和会话结束状态见 [EEPROM 验证记录](Validation/EEPROM-20260928/README.md)。

参数/HostLink/INS 存储尚未迁移。密集 EEPROM 访问触发现有 LIS/SPA 将 I2C BUSY 判作 OFFLINE 的上层策略，结束后恢复 READY；本轮未修改这一耦合逻辑，不能宣称共享服务实时性已通过。

## 2026-09-28 I2C4 周期传感器与后台 EEPROM 调度

实现周期保留槽、后台有界队列和防饥饿分发，FT24C128A 读写/校验均最多 32 B，LIS/SPA 使用 30 ms 完成信号量等待。20 组主机套件、九个父项目构建与专用 Debug/Release 均通过。最终连续 32.864 秒板测 20 项检查通过、整片恢复一致；LIS 约88–93 Hz、SPA约16 Hz，全阶段 READY，无总线错误/超时/恢复。测试前完整原始 Flash 已恢复并全区比对，MCU 运行原固件；GDB/J-Link 仍连接、无新增断点。具体取舍、时序统计、SHA 与未测边界见 [完整记录](Validation/I2C-20260928/README.md)。

## 2026-09-28 I2C4 Fast-mode 提速

先将此前全仓库工作提交为 `ebef330`，再将 IOC/Glue 调整为 400 kHz Fast mode，保留调度与 32 B 分段。20 组主机套件、九个父项目构建、Debug/Release 和时序检查通过；21 项板测全部通过，负载耗时 13.703 秒。数字 SCL 约 370 kHz，整片写含校验由 8.19 秒降为 5.12 秒，整片读由 2.57 秒降为 1.03 秒；LIS 约97–99 Hz、SPA约16 Hz，持续 READY，零总线错误/超时。最终 EEPROM 16384 B 与主机独立生成模式逐字节一致。

用户明确无需恢复：板上保留 Fast-mode EEPROMBringUp 和测试内容，MCU 运行、融合与加热关闭，GDB/J-Link 仍连接、无断点。未执行 CubeMX regenerate，未测模拟边沿或异常电气工况；首次候选的 HAL 时钟查询不支持问题及修正、全部统计/SHA/证据见 [完整提速记录](Validation/I2C400-20260928/README.md)。

## 2026-09-28 CubeMX CLI 生成

随后按用户明确要求，使用已有 `Board/generate.mx` 经 CubeMX 6.18.1 `-q` 执行 `project generate`，退出码 0。与生成前快照相比，CubeMX 仅更新 `Board/Core/Src/i2c.c` 的 TIMING 为已板测的 `0x10C7182D`；407 个 USER CODE 区域及其他 Board 文件保持一致。时序检查覆盖 IOC/生成代码/Glue 三处一致；独立 Minimal Debug/Release 和九个父项目构建检查通过。日志及边界见 [生成记录](Validation/CubeMX-20260928/README.md)。本轮没有烧录或操作调试会话。

## 2026-09-28 Board/Support 职责整理

Glue 重组为 Support/System、IO、Devices、USB，指示器策略独立为服务，USB 准备及 MCP 默认绑定收拢，固定指示设备硬件宏私有化。原 20 组基线、当前 21 组主机测试、九个父项目目标和 Minimal/Board/EEPROMBringUp 三入口构建通过。生成区、IOC、HAL、链接脚本和历史证据保持；没有生成/烧录或板测。详细验收、尺寸及保护检查见 [BoardSupport-20260928](Validation/BoardSupport-20260928/README.md)。


## 2026-09-28 托管任务、父项目 Kernel API 与循环统计

init/step/cleanup、协作生命周期和有界运行期线程/同步池已实现；SDK worker 统一框架与 SRNOS_ 命名，基础 CPU/ADC 流保留，新增 0x0003 循环统计。22 组主机、九个父项目固件、Minimal Debug/Release、EEPROMBringUp Debug 以及结构/ELF/上游哈希检查通过。代码/协议、内存预算和板端待测边界见 [ManagedTasks-20260928](Validation/ManagedTasks-20260928/README.md)。本轮未 Generate、烧录或板测；低干扰目标尚未由实机证明。

## 2026-09-29：上位机触发静态 IMU 校准

实现任意静止姿态陀螺零偏估计、异常拒绝、单份 EEPROM 校验保存及 CONFIG 0x09/0x0A 命令和状态查询；新值下次启动生效。25 组主机套件、91 个公共头/158 编译单元、父项目十种固件、命名/结构/内存与 Debug/Release 通过；没有烧录。静态内存增量、栈帧说明与板端待测见 [StaticIMU-20260929](Validation/StaticIMU-20260929/README.md)。

## 2026-10-10 IMUTurntable revision14制动与重力补偿

ID3双向40000raw/s、80%静止拟合重力前馈、回算抗饱和5/s；不恢复虚拟轨迹。增量构建、烧录及132.3秒完整流程完成，双IMU各132278样本无间断；ID3正/返编码器峰值75.2/118.4降至35.3/34.8°/s，目标27仍有误差。ID1低频起伏、ID2短促启动尖峰仍存在。未运行主机测试。探针退出后独立确认Hold继续承重，CDC1释放。见[详细记录](Validation/IMUTurntableGravity-20261010/README.md)。

## 2026-10-10 转台多姿态坐标辨识

revision16增量构建烧录通过，run18完整180秒/31步，无故障；双IMU各约18万样本无间断。24个分轴段辨识出外→内ID1→ID3→ID2，轴向RMS0.47/0.60°；两个留出的联动段向量速度误差RMS0.9～2.0°/s，但仍有约2.3°积分残差，不是精密校准验收。未写SDK安装矩阵/EEPROM，最终默认闭环承重、探针/CDC1释放；未运行主机测试。详见[坐标辨识记录](Validation/IMUTurntableAxes-20261010/README.md)。


## 2026-10-10 转台保存栈溢出修复

revision22 单独重放真实校准保存请求，捕获 HostComm 栈溢出且 EEPROM 尚未写入。revision23 去除工作项重复栈拷贝、HostComm 增至 3 KiB，已烧录；并行遥测下双 IMU 保存、重启加载逐位核对及相同数据去重通过，未重跑运动。见 [完整记录](Validation/IMUTurntableSaveFault-20261010/README.md)。
