# 迁移验证记录

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
python3 Tests/Build/check_structure.py
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
- CAN1–3 RX 的历史最小空闲栈各 412 B。源码有 32 个 `bsp_fdcan_rx_frame_t` 的局部数组，目标 sizeof 为 108 B，即 3,456 B；尚无溢出证据，但真实 CAN 负载下栈余量需优先复核。
- 复位后约 40 秒，MCP SPI xfer_calls/xfer_ok 均 15,958，busy/timeout/error 均零；外置 CAN init_errors=0、rx_frames/tx_frames 均零。仅证明此窗口内初始化及 SPI 访问，不证明 CAN 帧收发。
- USB ready=true，但日志 usb_configured=0、tx_bytes=0；CDC 枚举和端到端日志未验证，不能仅据此断定 USB 实现故障。
- GDB 栈展开出现相邻帧相同提示，未将其单独当作真实递归或栈损坏证据。

结论：本轮持续观察及一次软件复位均未复现旧启动屏障断言。暂停、断点和 BringUp 栈扫描会扰动时序；未完成实时负载、CAN/UART/USB 压力或整板验收，软件复位也不替代断电上电。

结束时已删除本次唯一新增的 KernelFailure 硬件断点，断点列表为空，执行 continue 让 MCU 继续运行。GDB/J-Link 会话保留并继续占用探针；未 detach 或关闭服务器。日志为 `/tmp/srnos-barrier-live-20260926.log`，后续会话活动可能继续追加。
