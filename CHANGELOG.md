# Changelog

记录 SirinEmbedOS 已完成的变更、验证结果与重要限制。尚未发布的变更归入 `Unreleased`，规划与约束见 `AGENTS.md`。

## Unreleased

### 2026-09-26

#### 启动屏障修复后的板端复测

- 经用户授权使用 J-Link V8.96 + ARM GDB 连接；板上向量表及代码段匹配修复版 BringUp ELF，未重新编译或烧录。
- 初次连接时已运行约 634 秒/127 次采样；继续后 648 秒/130 次。软件复位后在 40 秒/8 次及 99 秒/20 次采样，均未复现旧断言，IMU 数据新鲜，当前 Fault 标记为零。
- 记录 CAN RX 最小空闲栈仅 412 B、USB 尚无枚举证据，保留为后续验收项。未做 CAN 帧压力或完整实时性验证。
- 清除本次新增断点后让 MCU 继续运行，保留 GDB/J-Link 会话。取样值、固件 SHA 和日志路径见 `Docs/Validation.md` 文末。


#### 调试协作约定

- 在 `AGENTS.md` 补充 CLion MCP 与 Agent 直接使用 J-Link/GDB 的接入检查、单一会话所有权、暂停现场保护、ELF 核对、有界栈回溯及调试结束状态说明。
- 记录本机实测的 MCP 端口变化、J-Link 旧版符号链接及未知帮助参数仍进入连接流程的注意事项；明确这些路径和版本须重新核对。

验证：仅检查文档变更；未操作调试会话、修改固件、编译、烧录或复位。

#### 修复启动事件屏障误判

- 根据用户 J-Link/GDB 板端记录，系统已进入 RUNNING 并完成一次 BringUp 采样，但约 464 ms 时 Indicators 的等待返回 `0x3`，旧代码以 `== 0x2` 判断失败并进入 Panic/复位循环。
- 修复系统线程和应用声明线程两处等待：先检查 `osFlagsError`，再以掩码确认所需位全部置位。保留 WaitAll、NoClear 和无限等待，不更改线程优先级、启动里程碑或生成代码。
- 新增 Kernel 主机回归，使用真实 CMSIS 声明，模拟合并就绪位、缺位和错误返回，并检查等待参数与单次调用。纳入统一主机测试入口。

验证：13 组主机套件和结构检查通过；修复版 BringUp 的 Debug/Release 编译链接、基础 ELF 内存/TLS/异常向量检查通过。

硬件记录证明的是修复前的失效原因，修复后的板端验证仍待执行；本次不操作用户保留的 GDB/J-Link 会话，不烧录、复位或继续 MCU。修复固件在独立的 `build/BringUp-barrier-fix` 和 `build/BringUp-barrier-fix-release` 构建，保留 `build/BringUp` 原 ELF。

#### 迁移收尾与 BringUp 观测应用

- 增加 `Docs/Development.md`，将源码迁入完成与板端运行验收分开，列出启动/内存、固定硬件、串口协议、双 CDC/参数和父项目 API 收尾顺序。
- 新增可选 `Examples/BringUp`，在现有应用线程中每 5 秒采集启动状态、heap_5 余量、任务栈高水位、IMU 快照年龄和日志统计，提供调试符号与 CDC0 日志摘要。关闭本示例的启动业务参数恢复，保留固定服务默认行为。
- 根入口通过 `SIRINEMBEDOS_EXAMPLE=Minimal|BringUp` 选择应用，默认保持 Minimal；父项目构建回归加入 BringUp，第七种目标覆盖示例接入。同步 README、AGENTS 和示例说明。

验证：BringUp 独立 Debug 编译及基础 ELF 检查通过；默认 Minimal Debug 重配/构建通过且镜像资源保持原值；七种父项目目标、MSP/libc 覆盖与越界拒绝、结构检查通过。未重复运行未改动组件的 12 组主机套件，其前次结果见下文。

限制：未烧录或实测。全任务栈扫描会挂起调度器，适合低频资源观察，不能作为实时压力验证；`RUNNING`、USB 启动和 IMU 数据新鲜均不等于整板健康。SDK 默认 IMU 温控/校准行为继续保留。

#### Framework 源码整合与初始化冻结

- 将 prototype 的通用基础操作、Fault、内存、诊断、总线、固定设备、协议与服务源码直接纳入 SDK，移除对几十个 Framework 子仓库的组织依赖；父项目只引入一个 SirinEmbedOS 子模块。具体迁移/保留/排除项、来源与许可见 `Docs/Migration/README.md`。
- 公共基础操作及启动入口统一为 `SRNOS_*` / `SRNOS::`，应用使用 `SRNOS_ApplicationConfigure` 和 `SRNOS_ApplicationMain`。应用算法、控制状态机、业务线程和具体设备组合仍由父项目管理。
- 增加 `SRNOS_SystemConfig` 声明、冲突和启动预算校验、统一冻结及应用线程装配。CAN/UART/IMU/服务/线程等结构配置初始化后不可重载；服务注册在内核初始化前完成，资源与硬件启动按内核阶段执行。应用线程等待参数加载尝试及 USB 启动阶段结束；启动状态与具体设备健康分别观测。
- 整合三路原生 CAN、外置 CAN、SPI/I2C、IMU/EEPROM、板级指示器和诊断。默认集成硬件访问能力，不自动执行电机使能或业务运动。FatFs、LwIP 未迁入。
- Serial 成为 UART 的唯一 HAL/DMA/回调所有者，协议服务和原始通道通过初始化声明获得独占资源；普通 UART 默认未绑定。DR16、VT03、裁判、H12H、日志按通道拆分，UART7 保持固定 RS485 总线。端口参数、owner 和缓冲预算冻结，错误恢复不改变用途。
- 正常日志改用 Serial/USB sink，不再复位 USART1 或调用 Emergency。调度前日志仅捕获到有界 RAM 启动缓冲，避免触发 FreeRTOS 尚未就绪的临界区；stdio 明确提供正常输出和失败返回策略。
- 参数注册表初始化后冻结，默认只读，只有显式 `PARAMS_MUTABLE_BUSINESS` 的业务值可远程写入；系统结构配置不进入主机可写表。新增受锁保护的标量和一致快照、先校验后原子应用的批量写入。持久化加载完整验证后仅更新可写业务值，失败保留当前值；默认启动加载先于 USB 接收主机命令。
- 迁入固定 AL94 USB Composite **1.0.3** 的 Core/COMPOSITE/CDC_ACM，CDC0 用于日志、CDC1 用于 HostLink；保留 vendor 原文、许可和哈希，原 descriptor 补丁由构建目录派生。SDK glue 补齐 PA11/PA12，复用生成的 PCD。CDC class 使用静态存储，移除未使用的 libc 分配别名，不增加其他 USB class。
- Fault 向量、retained record、HSKP/Debug Watch 和 MPU/Cache 专用段接入 SDK；基础 API 更名不改变 Fault v2 二进制布局。默认紧急输出只保留 RAM 记录，可在初始化时显式预留；不自动占用串口。
- 最终链接采用 `Board/Linker/SirinEmbedOS.ld`，普通 data/TLS/BSS 放 D1，DTCM 保留热数据、RTOS heap 和 MSP；加入 D2 DMA/Watch/retained、D3 BDMA、ITCM 复制和独立 libc arena。Picolibc 静态递归锁接入 FreeRTOS，IRQ 不允许动态分配；生成基线文件保留原位置。
- 新增 `Examples/Parameters` 业务 PID 增益和快照示例、`Examples/PrototypeBindings` 串口用途示例；重写 README 和 AGENTS 的当前能力、父项目接入、初始化冻结与验证边界。

验证：12组主机套件通过；根与Board独立Debug/Release均链接通过；外部父项目6种固件（含无GC全组件、94公共头）通过。检查MSP/libc容量覆盖与越界拒绝、DMA银行、TLS及真实异常向量。完整命令、构建变体、来源校验及剩余问题统一记录在 `Docs/Validation.md`，不将旧基线的 size 数据用于本轮资源预算。

限制：未烧录，USB 枚举、UART DMA/IDLE/错误恢复、RS485 收发、CAN、IMU、FPU/TLS、缓存一致性和 Fault 仍需板端验证。默认初始化不代表外接设备存在或服务健康，具体业务必须检查相应状态。

### 2026-09-25

以下为当时的阶段记录，其中旧 API 名、链接布局和“尚未迁入”范围已由后续条目取代。

#### SDK 构建与 FreeRTOS 迁入

- 根目录改为可独立构建、也可由父项目 `add_subdirectory()` 引入的 SDK，提供 `SirinEmbedOS::Platform`、Board/Kernel 组成目标和 `sirinos_add_firmware()`。父项目决定工程名、固件目标和应用源码，SDK 作为子目录时不自动创建示例固件。
- `Board/CMakeLists.txt` 将生成清单接入对象库，在局部作用域隔离 CubeMX 对 `CMAKE_PROJECT_NAME` 的依赖；Board 独立入口复用同一 SDK，生成清单、基础链接脚本和启动汇编保持原样。
- ST Arm Clang 工具链适配将链接脚本与 map 从生成的全局参数移到 SDK/固件 target。Board preset 改用同一适配工具链；旧 Board 缓存需刷新，否则会继续加载原工具链。配置阶段检测该情况并给出清理提示。
- 从 prototype 导入 FreeRTOS **10.6.2**、CMSIS-RTOS2 wrapper/API、原 CM4F 浮点 port 和 heap_5；第三方文件保持字节一致，附来源、参考 commit、许可证和 SHA256 清单，没有升级依赖或引入嵌套子模块。
- 新增 SDK 管理的 FreeRTOS 配置、Kernel 启动和基础失败 hooks，以及 `Board/Glue` 接线。生成 `main()` 的 USER CODE 在 HAL 初始化后调用启动入口，SDK 启动唯一一次调度器，在 Application 线程调用父项目的 `OS_ApplicationMain()`；应用入口可创建线程或返回。
- 保留 1 kHz Kernel tick、TIM7 HAL tick、56 级线程优先级及 IRQ syscall 门限 5；SVC/PendSV 由 port 实现，SysTick 由 CMSIS wrapper 实现。启用 Picolibc 的每任务 TLS，关闭旧 Newlib reentrancy；没有迁入旧 HSKP trace 回调。
- 使用自维护链接片段保留 DTCM 64 KiB + AXI SRAM 128 KiB heap_5；两区均为 NOLOAD，heap_5 在内核初始化时注册并初始化管理结构，payload 不保证清零。链接检查 SRAM 边界，并确保 libc 的 `_end` 位于 DTCM RTOS heap 之后。
- 增加 `SIRINEMBEDOS_CONFIG_HEADER` 接口，父项目可统一覆盖初始应用线程栈（默认 4096 字节）、CMSIS 优先级（默认 24）和堆容量；同一构建内 SDK 使用一套配置。
- 增加最小 heartbeat 应用、接入 README、父项目 CMake/C++/TLS 构建回归，并同步 AGENTS 的已实现范围。

验证：ST Arm Clang 21.1.1 / Picolibc 下，根目录 Debug、Release 与 Board 独立入口均编译链接通过。Debug Flash **60,044 B**，Release Flash **47,340 B**；两者 DTCM **76,224 B**（含 64 KiB RTOS heap 与生成脚本的 heap/stack 预留）、D1 **131,072 B**。这些数字不包含应用后续增加的资源。

`Tests/Build/check_sdk.py` 通过：第三方哈希、含空格的父项目路径、父工程名保持、两个不同固件名、helper 与直接链接 Platform 两种接入、应用配置覆盖、C++ 构造/TLS 段、强 IRQ/hook 符号和 NOLOAD heap 布局均完成静态检查。生成边界另行核对：只有 `main.c` 的 USER CODE 区、项目维护的 Board 入口和 preset 发生修改，IOC 未改、未重新 generate。

限制：尚未烧录；实际调度、FPU 上下文、TLS 隔离与外设行为仍需板端验证。CMSIS 原有 `optimize("Os")` 被 Clang 忽略的警告仍在。完整 Fault/Debug Watch、运行统计、DMA/BDMA 段、libc malloc/new/stdio 的 RTOS 锁、其他 middleware 和系统服务未包含在本次迁移内。普通 heap_5 分配不能作为 DMA 缓冲使用。

#### 早前工程记录

- 补充 `AGENTS.md` 中 SDK、Board 与父项目的目录层级用途表、代码放置和依赖规则，明确固定硬件能力、系统服务与应用算法的归属；仅更新规划文档，未创建空壳目录或修改固件。
- 修复 CLion 普通配置仅选择 ST Arm Clang、未加载 CMake 工具链时被当作 macOS 工程的问题：根入口在 `project()` 前为独立构建设置默认板级工具链；保留显式工具链和父项目的控制权。不修改 CubeMX 生成内容。
- 使用 CLion 自带 CMake 4.3 / Ninja，在 `cmake-build-debug-starmclang` 刷新缓存，模拟不传工具链的 Debug 配置；配置、编译和链接通过，生成 `SirinEmbedOS.elf`。Flash 49,136 B、DTCM 7,216 B；仍有已有 CMSIS `optimize` 属性警告。未烧录或验证板端运行。

### 2026-09-24

#### 工程与架构

- 新增 SDK 根目录 CMake 入口、Debug/Release presets 和 ST Arm Clang 工具链路径适配，CLion 可直接打开 `SirinEmbedOS/`；复用 Board 生成源码清单，不修改生成文件。根目录构建目标为 `SirinEmbedOS.elf`，新增 IDE/构建输出忽略规则。
- 建立面向固定 STM32H723 主控硬件的 SirinEmbedOS 重构基线，参考工程为 `/Users/sirin/CLionProjects/h723vg-v2-prototype`。
- 在 `AGENTS.md` 明确单一 SDK 子模块的组织方式：SDK 提供固定硬件能力和公共服务，父项目管理特有控制算法、业务任务与配置。
- 写入 SDK 和父项目的目录规划、CubeMX 生成边界、CMake 接入与启动所有权规则。目录按需建立，规划中的系统能力尚未迁入。
- 明确 CAN、板载 IMU 等固定能力默认集成，UART 等可变用途资源保留配置和占用管理空间。

#### HAL 生成基线

- 板级配置位于 `Board/OMNICTL2_3.ioc`，CubeMX 生成内容集中在 `Board/`。
- 生成目标使用 CMake 与 ST Arm Clang；当前工具链配置为 `STARM_PICOLIBC`。
- 生成的构建清单未引入 FreeRTOS、FatFs、LwIP 或 USB Device 协议栈，后续由 SDK 独立接管。
- 保留 TIM7 作为 HAL 时基；生成的 C 中断文件未实现 SVC、PendSV、SysTick，为后续 Kernel 接入保留入口。
- ADC3 规则转换序列扩展为 5 个通道：IN10、IN0、温度传感器、VREFINT、VBAT；采样时间均为 640.5 cycles。
- 已确认 ADC3 生成配置使用软件触发、DMA One-shot、BDMA Channel2、半字传输及 Normal 模式。

#### 验证

- 根目录 Debug 配置与构建通过，使用 ST Arm Clang 21.1.1 / Picolibc，生成 `build/Debug/SirinEmbedOS.elf`。验证时将本机 ST Arm Clang 的 bin 目录加入 PATH；CLion 工具链环境也需要提供此路径。Release preset 已提供，尚未验证。
- 在 `Board/` 执行 `cmake --preset Debug` 和 `cmake --build --preset Debug -j 8`，ST Arm Clang 21.1.1 配置、编译和链接通过。
- 产物为 `Board/build/Debug/OMNICTL2_3.elf`；该次链接报告 Flash 占用 49,136 B，DTCM 占用 7,216 B。此数据仅代表当前 HAL 基线，不是完整 SDK 的内存预算。
- 编译出现 ST CMSIS 的 `optimize("Os")` 属性被 Clang 忽略的警告，未阻止链接。
- 已静态核对 ADC3 五通道、TIM7 tick 及外设 HAL 中断调用；未烧录或验证实机运行。

#### 已知限制与待接入项

- FreeRTOS Kernel、系统服务、应用接入 API 和目录规划中的其他组件尚未完成迁入。
- HardFault、MemManage、BusFault、UsageFault 仍为 CubeMX 生成的死循环处理函数；接入 SirinOS Fault 前需要统一所有权。
- USB HAL MSP 当前仅生成 PA9 VBUS GPIO 配置，未生成 PA11/PA12 的 GPIO 初始化；后续板级 glue 需要复核并补齐 USB 准备流程。
- 当前基础链接脚本尚未接入 SDK 的 DMA/BDMA 专用内存段；ADC3 BDMA 缓冲的 SRAM4/D3 放置与初始化需要后续实现。
- ETH、SD 初始化函数已生成，但未由 `main()` 自动调用；ADC3 尚未执行校准与启动采样。
- 当前 Board CMake 是独立板级固件工程，尚未完成供父项目引入的 SDK target 接入。
