# Changelog

## Unreleased — 2026-11-20

- 在 MP257-M33 分支以 DMMotor 替换 DM4310/DM4340 旧公共接口，覆盖 DM4310、DM4340 V1.1、DM6006 和 DM8009P。注册时显式指定量程和反馈回绕配置。 已通过 M33 和 H723 构建；主机合成帧验证量程、回绕、失联和发送行为，四款电机的实际反馈待板验。

## Unreleased — 项目许可

- 自有代码采用 [MPL 2.0](LICENSE)。[第三方许可说明](THIRD_PARTY_NOTICES.md) 列出固件依赖、板级模板和供应商驱动的适用条款，以及 MCP2518FD 源码公开再分发尚待确认的范围。

## Unreleased — 父项目接入文档（2026-11-18）

- 新增[初始化配置指南](Docs/Config.md)与[单文件任务示例](Examples/StartupTask/README.md)，首页和 QuickStart 优先说明 VT03/VT13、业务任务、日志、遥测和 EEPROM 参数；补全模块入口并校正过时示例。固件与文档验证结果见[文档变更记录](Docs/CHANGELOG.md)。

## Unreleased — ST Arm Clang 编译诊断（2026-11-18）

- 修正 CMSIS 启动函数声明的编译器判断，保留其他属性诊断。Minimal Debug/Release 构建通过，两份可烧录镜像分别与修改前一致。详见 [板级变更记录](Board/CHANGELOG.md)。

## Unreleased — 源码文档整理（2026-11-17）

- 文档导航直接指向现行接口、示例及第三方组件。清理前后使用同一工作区和工具链完整构建 Minimal Release，两份 264160 B 固件镜像逐字节一致。

## Unreleased — 框架修复与维护收口（2026-11-11）

- DM4310/DM4340/LK8016E 组合 setter 与发送端使用短临界区发布/复制整组命令，DM4340 MIT 范围在发送和反馈换算时一致读取；连续独立 setter 不构成事务。Classic CAN 严格校验 ID、flags、DLC、过滤器和组合项，不再掩码纠正，one-shot 拒绝超过 INT32_MAX 的 TTL。
- 功率限制按模型分组占比分配有效实测总功率，无比例时统一缩放；限幅和变化率先于模型估算及最终功率缩放，last_cmd 保存最终输出。OLS 改存相邻间隔并重建窗口相对时间。SPI 在完成发布时保留首次取消原因；SirinLink 以订阅代次覆盖发布、取消、重订阅和断连窗口，构建中的槽由原构建者回收。
- RS485 Start/IsFrozen 改为私有装配、worker 改为文件内部函数；删除空恢复入口及公共可写统计，GetStats 改为 bool + 输出副本。删除功率配置中未生效的 cmd_type、z_target_joule、对应常量和 overflow_cnt。父项目须整体重编译，现行接口见 [RS485](Source/IO/RS485/README.md) 和 [功率约束](Source/Algorithms/Power/README.md)。
- 协议模块的纯串口校验由系统预检与 Init/Bind 共用；Network 用固定启动期 UDP 回调槽承接 Message。Parameters 展开控制流并复用 Core Endian，持久化格式、CRC 和事务语义保持。统一 SirinLink/Telemetry 注册顺序和容量说明。
- 各服务持有栈/队列常量及私有预算函数，Kernel 汇总并在注册后终检。Logging 预算纠正为 22288 B；默认全 Classic 启动预算为 163088 B，每个 FD 通道增加 5284 B。InputMessages 为保留默认 heap 容量，显式关闭可选托管周期统计，注册后预算为 190536/196608 B；SystemMonitor、CPU 统计、线程数量及优先级保持。预算值来自 ARM 布局和源码核算，不是运行水位。
- Classic 上下文由五份降为四份，符号从 59920 B 降至 47936 B。FD 注册元数据保留静态，TX 存储每个启用 FD 的通道在内核初始化后分配 5252 B；最大空闲块预检避免 malloc-failed hook 抢先终止，失败先清理本次分配再中止启动，运行期不分配。FD 静态 context 从 23312 B 降至 2336 B，净释放 20976 B；全 Classic 不分配 FD TX 存储。
- ST Arm Clang 21.1.1 离线编译链接通过：Minimal Debug/Release、InputMessages Debug、VT02VT12 Debug、从 QuickStart 提取的临时父项目，以及全 Classic、混合 Classic/FD、全 FD、串口/网络四个临时接入固件。接入片段覆盖新 RS485 签名、三个电机类及功率/OLS；父项目未获得 HAL/RTOS/私有 include。102 个公共头的 179 项独立 C11/C++17 编译全部通过。构建仅保留已有 CMSIS optimize 属性警告。

Minimal ELF/map 对比（字节，包含对应区域的固定预留）：

| 配置 | D1 修改前 → 修改后 | D1 余量增加 | ITCM 修改前 → 修改后 | ITCM 余量增加 |
|---|---:|---:|---:|---:|
| Debug | 321600 → 300896 | 20704 | 62976 → 50976 | 12000 |
| Release | 321632 → 300896 | 20736 | 62976 → 50976 | 12000 |

- 当前 Minimal D1 余量 26784 B，ITCM 余量 14560 B；ITCM 总收益包含对齐变化。D2/D3 用量保持 16064/544 B，DMA、BDMA、Ethernet DMA 及 retained 段地址/大小均不变；heap_5 仍预留 DTCM 64 KiB + D1 128 KiB，网络缓存分配方式不变。
- 源码复核覆盖非法 CAN 参数、SPI 完成/取消交错、遥测取消后重订阅、FD 失败清理和电机快照边界；Parameters 规范排版后比较仅有 Endian 委托差异。本轮未运行行为模拟或主机测试，未新增测试套件或默认测试依赖，临时源文件与产物均在仓库外；未 Generate HAL、烧录、操作调试会话、提交或推送。并发时序、功率控制、取消确认、FD 启动失败及混合负载仍待实机验证，不将编译通过视为行为通过。

## Unreleased — 内置遥测固定周期（2026-11-05）

- CLI 复用既有传感器协议周期表，订阅/退订使用各流固定周期；修复默认 `telemetry read --streams system,ins` 被设备以 INVALID_ARGUMENT 拒绝的问题。`--hz` 继续用于 SOURCE；固件 API、采样频率及控制行为不变。
- 主机假客户端覆盖全部内置流订阅/退订周期，实板默认 system/ins 订阅、解码及退订通过，USB 只读验证单独记录于父项目；SDK 不携带原始现场数据。


## Unreleased — main 合并 CmdLineTool 与 VT02/VT12（2026-11-05）

- 合并主机 `srneos` CLI 与 main 的 VT02/VT12 UART、SirinRMT 接入；保留双方 API、文档及工具，未新增固件线程或修改生成边界。
- 合并后 Minimal Release 和父项目 VT02 控制 Release 编译通过，VT02 协议/Input 集成 ASan/UBSan、父项目控制回归、CLI Python 编译及计划 dry-run 通过。真实遥控端到端收发与运动不由这些主机验证证明。

## Unreleased — SirinRMT over VT02/VT12（2026-11-04）

- `SRNOS_SirinRMTRegister` 支持显式绑定 VT02 串口端点，解码 `0302` 的 30 字节 SirinRMT 载荷。UART 接收流重置会使对应输入失效，并保留会话序号以拒绝重放。未注册时原始载荷路由不变。
- 发布前复核：VT02 A5/CRC、配置/堆预算及 UART→Message→SirinRMT 的通道、使能/失能、重放、重置和过期 ASan/UBSan 主机回归通过；VT02VT12 示例 Release 编译链接通过。本次未连接硬件或执行运动，未将本地主机测试和构建产物纳入 SDK 源码。

## Unreleased — VT02/VT12 正式接入（2026-11-04）

- 整合旧图传 RX 与显式 TX/Flush，补齐方向、载荷生命周期、超时、并发与栈合同；增加双向接入指南、VT02VT12 公共 API 示例和构建选项。保持已验证的 A5 编码、单一 Serial 所有者及 InputIO 调度，不增加自动发送或业务映射。
- 此次本地整理不连接或烧录设备；固件、接线、选手端实现及原始验证资料单独由父项目保存。以此前有限包双向验证为兼容性基线，硬件改动后的运行仍需重新验收。

## Unreleased - VT02 explicit TX（2026-11-03）

- 复用内置VT02 Serial通道增加任务上下文Send/Flush，用于应用显式发送完整A5帧；发送完成与远端接收分开，不新增后台流量或线程。父项目Debug、C11/C++17公共头和SDK集成验证通过；当前VT02/VT12实机三包30字节上行通过NS原文/序号/CRC核对，持续吞吐及其他固件未验。

## Unreleased - Serial diagnostics（2026-11-03）

- 新增只读 `SRNOS_SerialGetPortStats`，复用现有统计快照供应用观察已由协议占用的端口；不暴露通道、不消费接收队列，不新增任务或缓冲。父项目Debug构建、C11/C++17公共头、既有VT02集成测试通过；父项目CDC1板端连续读取验证通过。UART9有效业务数据仍未收到，不能作为图传通信已通的结论。

## Unreleased - VT02/VT12（2026-11-01）

- 按官方 2024 V1.6.4 增加 115200/8N1 的旧图传 A5 RX；0304 发布独立键鼠 Input，0302 原样送独立 VT02 Message transport。与裁判常规链路、VT03/13 互不混用。
- 新增 vt02_port、公有解码/绑定接口、串口格式/冲突检查与 InputIO 堆预算；新增静态缓冲，沿用原任务、优先级与栈，SDK/父项目需整体重编译。无旧图传 TX 或运动映射。
- ASan/UBSan 主机验证覆盖官方 CRC 表生成的固定向量、所有分片边界、粘包、符号极值、CRC/错误长度/未知命令/嵌套载荷、随机流、实际 InputIO/Input/Message 分发、按键边沿、独立时效、UART 异常/恢复、时钟回绕及有界轮询；父项目 Debug/Release 编译通过。未烧录，未进行真实图传或板端时序验证。


以下记录保留各次变更、验证结论及未验边界，仅适用于当时版本和负载。原始测试日志与实机转储不随源码分发；当前用法以 README 和专题指南为准。

## Unreleased — srneos CLI 实现（2026-11-04）

- 新增 SDK 主机 Python 包 `srneos`，统一 J-Link/目标识别、镜像烧录及独立回读、地址读取/保存、CDC 日志与 SirinLink 遥测/参数/命令、SirinRMT 有限输入测试、主机测试和单会话计划；安装和命令文档位于 `Tools/CmdLineTool/`。
- PyLink 调用隔离为有期限子进程；USB 共用请求/事件分发和有界写盘；明确区分读取、比对、保存以及发送/业务执行证据。复用现有 Python 编解码，父项目无需复制实现，固件公共 API、调度、生成边界和内存布局不变。
- 42 项主机用例、原 SDK Input/SirinLink Codec 回归、PTY 字节流及格式检查通过；wheel 构建成功。没有连接真实探针/板卡、烧录或运动；对应实板和 Windows/Linux 硬件验收仍待完成。

## Unreleased — srneos CLI 设计（2026-11-04）

- 新增 `Tools/CmdLineTool/` 体系设计、命令/状态/证据合同、结果 Schema、配置示例及实施验收计划；通用能力由 SDK 持有，父项目通过配置和公共协议调用。
- 本次为设计文件变更，CLI 尚未实现；未连接硬件、烧录或运动，不改变固件 API、生成边界、调度及内存布局。

## Unreleased — CI/CD（2026-10-27）

- 新增独立 `CICD/` 构建与产物交付工具，根 GitLab 入口引用目录内配置；不恢复 Tests、BringUp 或原始验证资料。SDK 与父项目分别构建 Debug/Release，可选发送已配置的通知。生产代码、公共 API、调度与内存布局不变。

## Unreleased — SDK 内容整理（2026-10-25）

- 精简公开文档中的私有机构调试历史和重复板测摘要，集中网络资源合同与历史验证索引；保留本地私有测试。
- 移除废弃的 `SRNOS_RefereeTxInit()`；发送仍由启动配置和 Serial 服务初始化。`ParametersDumpDebugPort` 保留入口，改用正式 Logging 队列并报告失败。
- 删除 `Watch.c` 占位编译单元、无调用的 `ParameterBinary.cpp` 和私有 DebugPort 旧发送接口，保留正式诊断与参数能力。CMSIS-DSP 快照与 WS2812 critical 实现保留，说明按实际集成状态修正；IMU/INS 实现和相关主机工具未改动。
- 删除三项未装配的 GCC/Newlib 模板；生成脚本仅在生成与时序检查通过后，将这三项重新生成的文件归档移出源码树。
- SDK Minimal、Board、父项目 Release 和 12 个集成固件目标通过；101 个公共头、178 个 C11/C++17 单元、结构和文档链接检查通过。主机回归 37/38 通过，既有 IMUTurntable 回零断言（`test_control.cpp:25`）仍失败；本轮未实际运行 CubeMX、烧录或进行板测。

## Unreleased — 源码分发与文档整理

- 2026-10-24：公开树保留 SDK、API 示例和通用工具，移除原始验证归档及固定路径脚本。测试源码与五个 BringUp 示例恢复为本地私有内容，由 Git 忽略且取消跟踪；测试构建适配留在私有父工程。
- 清理个人路径、内部端点和设备标识；合并重复文档说明，移除失效引用，修正私有测试指南。生产实现、公共 API、板级配置、调度及内存布局未改动。
- SDK Minimal、InputMessages 和父项目 Release 重新构建通过；父项目固件与清理前逐字节一致，第三方 362 项哈希检查通过。此项为源码树清理，不表示 Git 历史已脱敏或许可证已补齐。

## Unreleased — W25Q128 Blackbox

- 2026-10-24：新增 NOR 私有驱动、板级 SDR OSPI 适配、12 MiB 可恢复环形记录和 SDK Blackbox 专用 worker；父项目仅用公开入队、异步读回/取消和状态接口。其余 NOR 分区预留，不含 DFU。
- SystemConfig 新增启动期 blackbox 配置，默认关闭，启用后将服务对象、队列、4 KiB 栈纳入保守 heap 预算；SDK 默认 heap 不变，公共结构需整体重编译。
- 新增 NOR/Blackbox 主机套件及系统配置预算/父项目接入回归。主机故障注入与有限次板端结果分别记录，不将模拟断电视为实际供电掉电验收。
- 2026-10-24：修复 NOR 首次板测在发命令前初始化失败：当前 HAL 不支持通过 `HAL_RCCEx_GetPeriphCLKFreq()` 查询 OSPI，改为校验固定 D1HCLK mux 后读取 HCLK 并向上取整分频。NOR 的 33 个 transport 与 13 个 HAL 主机场景通过 ASan/UBSan；新增未知 mux 拒绝回归。
- 修复后独立 Blackbox 板测识别 `EF4018`；20×224 B、序号 1..20 跨扇区，committed=20，写入/提交/完整读回合计 265 ms。MCU 复位后只读相同 20 条通过，105 ms、当前启动 committed=0，两轮读写错误均为 0。原始测试资料不随源码分发。供电掉电、满环回绕、脏扇区擦除、100 Hz 长期负载和栈水位仍未实板验收。

## Unreleased — SirinRMT PLANAR_VELOCITY

- 增加 kind=6 的有限 float32 vx/vy/wz 编解码及 Python CLI，旧 v1 类型、30 B 线长、会话与顺序语义不变。
- Input 公共结构扩展独立速度快照、能力声明、时间/有效位和 motion_epoch；切换清除旧运动/ENABLE，保留 DISABLE 身份。SDK 不包含机器人限速/档位策略；需要整体重编译。
- Input/Message/物理解码 ASan/UBSan、C/Python 黄金向量、公共头、结构检查及父项目 Release 通过；Jetson 双向 100 Hz、两类运动输入六方向及停止/切换板测通过，原始记录由父项目保管。

## Unreleased — 启动标识和 UDP Python codec

- 增加 SRNOS_SystemBootId()：板级 H723 RNG 在应用配置前生成非零 64 位启动标识，初始化有界、失败阻止启动；无新增线程/中断/heap，生成文件与链接分区不变。
- Python codec 增加 pack_raw/unpack_raw，SirinRMT UDP 复用；原 V2 和串口 COBS 合同不变。
- 公共头、结构、Input/Network/IMU telemetry 专项和父项目 Release 构建通过；EngNaviTest 实板复位后启动标识变化、NTP 恢复，UDP 100 Hz 双向与六方向架空联调通过。板测原始记录由父项目保管，不代表随机数质量认证或长期压力测试。

## Unreleased — SirinLink 命名与 SirinRMT DISABLE

- 2026-10-21：HostLink 当前公开 API、路径、宏、启动配置、实现、示例、测试和工具统一改为 SirinLink，不提供旧名兼容别名；HostComm 仍是传输任务名称。外层 V2 编码、COBS/CRC、service/message ID 保持。
- SirinRMT v1 保持 30 字节，增加 DISABLE（kind=5、全零 body），Input Frame/Snapshot 增加独立停止请求身份；保留输入字段及时间，清除旧 ENABLE 有效性。应用负责权限，需同时消费两种请求并对同周期停止优先；INVALIDATE/重新同步隔离旧请求。
- 公共结构变化要求 SDK/应用整体重编译。100 公共头、176 C11/C++17 单元、公共命名/依赖检查及 12 个集成固件目标通过。SDK 全量 35/36 通过；既有转台回零断言在隔离 HEAD 复现，相关 7 个依赖文件未变化。Input 与 SirinLink 专项通过，七来源示例 heap 预算为 192672/196608 B。未烧录。

## Unreleased — NTP 与系统绝对时间

- 2026-10-21：新增 `Services/Clock.h`，任务可读取一致的 uptime/Unix UTC 微秒、有效性、holdover、校时 generation 与 RTT；原有 RTOS/DWT 计时不受授时影响。
- Network 可选 SNTPv4 客户端，启动冻结服务器和单次/周期配置；复用现有网络线程、lwIP heap 与自动分配的临时端口。完成无效回复检查、四时间点补偿、退避、KoD、断链隔离与 2036 era 处理；不修改 ThirdParty/CubeMX。
- Clock、真实 lwIP Network、父项目测试和公共头/结构检查通过，Debug/Release 与完整边界见父项目 NTP 验证记录。SDK 全量测试中的既有 IMUTurntableBringUp 回零断言失败，该独立算法测试不编译本次时间/网络实现。未烧录、未配置远端 Jetson，实机精度待验。

## Unreleased — W.Bus 单次 LOST 不再使整个来源失效

- 2026-10-21：内置 W.Bus 适配层丢弃仅带 FRAME_LOST 的帧，不刷新 Input 字段/时间或改变 epoch；解码器仍累计 LOST。FAILSAFE（包括与 LOST 同时出现）及 UART 流异常仍立即失效。更严格的超时与重新使能仍属于父项目；无公共 API、调度、硬件或内存布局变更。
- SDK 输入适配/独立 W.Bus 回归及父项目安全回归验证短丢帧、连续丢帧过期、FAILSAFE 优先、同周期失效恢复、50 ms 控制边界及重新使能。编译/烧录与实板范围见父项目 2026-10-21-WBusLoss 记录，不将模拟注入视为真实无线失控试验。

## Unreleased — 明确的输入使能请求

- 2026-10-21：SirinRMT v1 增加 kind=4 ENABLE；Input Frame/Snapshot 保存请求计数、时间和 epoch，失效/恢复隔离旧请求。SDK 不执行机器人使能，不改变通道、按键、相对量时效；应用消费请求并实施安全门。Python 工具增加默认关闭的 `--enable`，只发送一次。公共结构扩展须整体重编译。
- Input/Message/SirinRMT、物理适配器和 C/Python 黄金向量通过；公共头 C11/C++17 检查和父项目 Debug/Release 链接通过。七来源示例主机 heap 预算 192616/196608 B；未新增线程、修改 CubeMX 生成区或烧录，板端运行与栈水位待验。

## Unreleased — 输入与消息

- 2026-10-21：通用 Message 分发与独立 Input 快照/事件、SirinRMT v1 接入 HostComm/UART/UDP/图传；保留 DR16/VT03/13/W.Bus，删除旧 RC 仲裁/固定五通道 API 和图传全局命令 FIFO。新增 InputMessages 示例与 Python 编码器；本轮未烧录。

- 验证：InputMessages Release 链接、99个公共头的C11/C++17检查、12个SDK集成固件、源文件覆盖及内存布局检查通过；新增输入/消息测试与真实lwIP第五端口测试通过。全套主机回归仍有未改动的IMUTurntableBringUp控制测试第25行断言失败；与本次输入代码无依赖的旧测试问题单列保留。未修改父项目、未烧录或接管PCB。

## Unreleased — W.BUS 串口解码

- 2026-10-20：新增公共 `Services/RemoteControl/WBus/WBus.h`，支持 100000/8E2 的 25 字节 W.BUS/S.BUS 兼容帧、16 路原始通道、数字 17/18、丢帧/失控标志及有界 Serial 轮询。不增加线程，不接入 RC 仲裁。新增 API 不修改既有结构；使用方式见 [WBus](Source/Services/RemoteControl/WBus/README.md)。

## Unreleased — 图传双向数据与遥测

- 2026-10-20：新增公共 `Services/VideoLink.h`，为已有 Telemetry handle 注册图传周期、稳定 wire ID 与新鲜度上限。VT03 单接收所有者拆分 `0311` 和遥控帧，原 RC worker 自动聚合 `0310`，40 Hz 总限速及有界队列。默认关闭，不改变电机或遥控仲裁语义。使用见 [VideoLink](Source/Services/VideoLink/README.md)。
- RemoteControl 配置/VT03 结构新增字段，父项目须整体重编译。无新增线程/优先级/硬件引脚变更。注册、发送节奏、背压、失效值、CRC、混合帧和 C5 解码通过主机测试；全 SDK 构建及公共边界检查通过。具体板端结果由 EngNaviTest 父项目验证记录维护，不推广为线速或长期热稳定证明。

## Unreleased — SDK 使用文档整理

- 2026-10-17：统一文档职责：各级 `CHANGELOG.md` 记录修改原因、日期、验证结果及历史结论；README 和专题文档描述当前用法、配置、示例与限制。规则写入 `AGENTS.md`，重建根入口与 Docs 导航，新增独立 [QuickStart](QuickStart.md)，提供可完整构建的父项目、周期任务及资源查询示例。
- 模块、示例、工具及专题说明按现有公共 API 核对，修正旧 include 路径、线程数量/优先级、传感器校准归属及检查命令。47 份历史验证报告逐字归档至对应 CHANGELOG；原验证总索引归档时仅修正相对链接，原始日志、数据和固件哈希保留。
- 离线验证：QuickStart 父项目和 Minimal Debug 固件均完整编译链接通过；95 个公共头的 166 个 C11/C++17 检查单元、49 个模块/专题示例编译单元、QuickStart 资源片段与结构检查通过；两条 HostLink 字节样例与编码器一致并成功回解。237 份 Markdown 的本地链接和锚点检查通过。
- 与整理前基线比较，1844 个非 Markdown 文件哈希全部一致；本次仅修改文档。PCB 离线，未连接、烧录或进行板端测试。

## Unreleased — UDP 接收事件锁存

- 2026-10-17：按端口提供启动冻结的纯接收选择器，支持最新值替换及幂等事件锁存；普通 UDP 默认行为保持。锁存事件跨排队期限和断线保留至消费，SDK 不解析底盘协议。配置/状态结构扩展，使用方需整体重编译；不改 PHY、DMA、线程优先级和周期。完成真实协议栈主机回归及父项目 STOP→CAN 软件集成验证；PCB 离线，未烧录或板测。

## Unreleased — 静态 IPv4

- 2026-10-14：固定 lwIP 2.2.1 / IPv4+ARP+UDP+ICMP，默认 IP 192.168.1.50/24；网络、可选网关和 UDP 端口由上层启动配置冻结。Network 单线程持有协议栈和二层收发，不改变 MCO/RMII/DMA/IRQ。新增 6 KiB 栈、7 KiB heap 预算及约 22 KiB 静态 D1；明确排队期限、link epoch 和原始接口所有权。主机/构建及父项目板测记录见 Network 文档。

## Unreleased — Ethernet 二层接入

- 2026-10-13 板测：无运动测试固件 100 Mbps 全双工二层回显 14+600 帧全部通过，连续窗口约 66 s，零超时/内容错误，MCU 64.88..66.56°C；94 公共头/12 SDK 目标和父工程 Debug/Release 通过。SDK 主机 30/31 套通过，已有转台断言在基础提交复现；正常底盘并发、线速、物理拔线恢复与长期热平衡未验。

- 2026-10-13：新增 Ethernet 冻结配置、LAN8720A 链路管理、原始帧 API 与低优先级 worker；ETH IRQ 7、D2 DMA 新增 9216 B，现有时钟/MPU/生成区不变。默认关闭，父项目显式开启；详见 Source/IO/Ethernet 文档与父工程实测记录。

## Unreleased — CAN 时间基准修复

- 2026-10-12：统一原生/外置 Classic/FD CAN 的回调、发送 TTL、适配器周期与重试到 `SRNOS_TimeMs()`，修复应用读取反馈年龄时混用 HAL 原点的问题；未绑定 backend 的截止点保持同域。公共函数签名和线程/内存布局不变。
- 增加 HAL 领先 12 ms 的回归、绑定前入队和回绕验证；Router/Native/External/FD/六种 Motors、公共头与 父项目 Debug/Release 构建通过。移除父项目补丁后板上 13/13 在线，年龄正常；CAN4/FD/物理断线本轮未板测。

## 2026-10-10 诊断与校准

- 默认致命 Panic/Fault 经 USART1 PB14、115200/8N1 输出，保留启动期自定义覆盖；仅异常时接管端口。主机、固件构建及物理串口输出/复位验证通过，边界见 [Core](Source/Core/CHANGELOG.md)。
- 修复保存请求的 HostComm 栈压力，去除重复工作项副本并增加 1 KiB 栈；协议和 EEPROM 格式保持。资源变化见 [Kernel](Source/Kernel/CHANGELOG.md)。
- IMU 校准 v2 增加陀螺对角比例，兼容 v1 加载，保存后重启生效；主机与构建检查通过，多姿态实机校准精度仍待验证。格式与应用规则见 [INS](Source/Services/INS/CHANGELOG.md)。

## Unreleased

- 2026-10-06：CAN4 合并为 P1/4 KiB 单服务，MCP 改用可仲裁 SPI 序列与 UA 确认，FD 生产时序对齐，并优化时间换算与通知。29 组主机、完整 SDK 十一目标、Debug/Release 及内存检查通过，ITCM 不增加；联合实测与未验边界见 [CAN4 板测摘要](Source/IO/CAN/CHANGELOG.md#can4-20261006)。

- 2026-10-06：完成CAN1/CAN4共享SPI与双IMU压力测试，新增专用应用和逐窗SPI/IMU观测。Classic各3k因生产周期不足未达标；FD5M及8M对齐配置64B各3k出现MCP RX溢出；默认FD8M仲裁采样点不一致，测试专用80%对照通过但生产未修改。双IMU/INS保持1kHz；优化前后结果见 [CAN 历史验证](Source/IO/CAN/CHANGELOG.md)。

- 2026-10-06：完成 CAN1↔CAN3 实板 Classic/FD 六模式、356 帧格式用例与54档负载；Classic8B及FD8M64B各3k收＋3k发持续60秒零损失，整板CPU32.00%/35.13%，FD5M64B同档10秒通过；低速64B过载按背压/TTL计数。双IMU/INS各1kHz连续，原生RX最小栈余3092 B。新增独立板测应用、原始bin和统计；原1MiB Flash完整恢复并读回一致，探针释放。身份记录CORRUPT及LIS偶发低采样另记待查，未写EEPROM；CAN2/4、UART及Bus-Off物理恢复仍未验。

- 2026-10-03：四路 CAN 启动时独立选择并冻结 Classic/FD，默认 Classic 1 Mbit/s；新增 FD 原始订阅/one-shot/统计公共 API，应用显式指定仲裁、数据速率及 BRS，协议留给父项目。FD 精确长度 0..8/12/16/20/24/32/48/64 B、TX BRS 固定/RX 保留实际 EXT/BRS/ESI，现有 adapter 仅允许 Classic 1 Mbit/s。每路 32 订阅/64 TX 槽固定在 D1；四路上下文 23312 B，Classic 59920 B ITCM 保持。
- 2026-10-03：原生 FDCAN 改用 PLL2Q 80 MHz，各路固定 0x300 word Message RAM 窗口；FD 8 RX/28 TX、64 B 共 676 word。MCP FD 16 RX/12 TX、64 B 共 2016 B，40 MHz/SPI 10 MHz 和 vendor 保持；启动配置与最终模式有界确认，拒绝冻结前初始化。原生 RX 每轮 32 帧预算改为每次搬运 4 帧，恢复 FD 回调栈余量而不扩栈；CAN4 长 TTL 换算防溢出、完成统计排除纯 RX 共享 IRQ。P0/P1、八个 worker、heap 不变。局部主机回归通过，PCB 离线，新增时钟/FD/TDC/吞吐均待上板验证，64 B 不能套用 Classic 虚拟负载结果。
- 2026-10-03：FD 最终离线回归为 29 组主机、十一种父项目固件、93 个公共头/162 个 C/C++ 编译单元、14 条应用 include 隔离与 117 个当前 SDK 对象检查通过，Debug/Release ELF 检查通过。Release D1 277888 B、Flash 218720 B，ITCM 63104 B（余 2432 B）；原生 RX 回调前静态调用链降至 Debug 912 B / Release 896 B，各节省 3016 B，不代表含应用回调/TLS/异常的动态栈验收。

- 2026-10-03：按用户最新要求调整为 P0 CAN1/2/3＋BMI/LSM/INS（48），P1 CAN4＋Serial/RC/Referee/RS485（47）。SPI4依赖49、IMU优先且两级有界防饥饿；NVIC P0=5、P1=6（UART DMA统一6）。公共API/容量/线程数不变；离线，未烧录或追加提交。

- 2026-10-03：提交既有重构基线 820b777 后，按每路 3 kHz RX＋3 kHz TX 优化 CAN：直接任务通知、冻结路由直读、group 按需初始化、批量完成通知、CAN4 减少 SPI 查询、Release 热路径 -O2；CAN/SPI/Serial 与 IRQ 优先级按通信顺序调整，SPI 保留防饥饿。修复原生满环竞争、HAL Bus-Off 回调/有界恢复、CAN4 初始化失败句柄与六种电机反馈并发/旧时间离线误判。公共函数签名和资源容量不变，电机私有布局变化需重编译；28 套主机、十个父项目、Debug/Release 和 ELF 检查通过。Release 命名工具仍有 13 条基线已存在的标准库/TLS 误报，未绕过。PCB 离线未烧录，实际四路吞吐/IMU 延迟待验。

- 2026-09-29：以释放 RAM 为目标，将 CAN Router 上下文和 Debug Watch 迁入 ITCM；新增 SRNOS_ITCM_BSS、64 位清零/复制及容量/对齐检查。Release 释放 D1 59904 B、D2 2848 B，ITCM 余 2528 B；heap、线程、DMA/BDMA、协议和 EEPROM 不变。25 套主机、父项目与 Debug/Release 通过；板端启动/软件复位/监测，CAN 未短接不做外部吞吐。

- 2026-09-29：按实测提高静态校准陀螺/加速度峰值/温漂拒绝阈值，新增噪声接受及运动拒绝回归；INS/HostLink、结构、Debug/Release通过。两次实板双IMU保存成功，复位加载revision2，保留新固件及EEPROM；不改变遥测，见 [INS 历史验证](Source/Services/INS/CHANGELOG.md)。

- 2026-09-29：完成首次持久化/静态校准板测，参数保存及复位加载、SAVE期间WRITE隔离、CRC与EEPROM恢复通过；静态判定噪声阈值不适配，校准未通过，8路1kHz变量实收约7284标量/s。新增仅EEPROMBringUp的显式主机备份恢复入口，未调整生产算法；最终Minimal Release运行且释放探针。

- 2026-09-29：整理当前文档入口：新增 Docs/README.md 导航和 Docs/Storage.md 持久化总览；补齐 COBS/CRC 帧样例、消息表、CONFIG 01–0A 与 SOURCE V1/V2，澄清记录/参数身份、默认加载和示例开关。同步模块与工具说明，保留历史板测；只改文档/索引，协议与固件行为不变。 已检查 20 篇文档的 186 个本地链接/锚点、COBS/CRC 样例与字段长度，现有 Codec、参数协议、Storage/EEPROM 主机测试和结构检查通过；未重新编译或烧录固件。

- 2026-09-29：实现上位机手动静态 IMU 陀螺零偏校准（任意静止姿态），保留有效加速度校正，约 5 s 观察及异常拒绝后保存单份 EEPROM，新值下次启动生效。新增 CONFIG 0x09/0x0A、完整错误/部分保存状态，复用 HostCmd，无新增线程或动态工作区；验证范围为主机与构建，未烧录或实板验证。

- 2026-09-28：实现FT24C128A单份CRC32身份/校准/业务参数存储，父项目Persistent与只读Telemetry声明，稳定ID升级及generation/dirty；移除旧24C02依赖。启动2秒加载期间温控继续，校准下次启动应用、无效明确降级；SOURCE V2因08/09已占用改用10–14。无新增生产线程，固定工作区计入map；25套主机、90公共头/156编译单元及闭包、十个父项目固件、结构/命名/第三方哈希和Debug/Release通过。D1剩余约13.7KiB，未缩减线程资源；本轮不烧录，板端掉电/高频组合负载待验证。

- 2026-09-28：CAN1/CAN3 外接吞吐板测发现并修复 weak 声明导致 RX/TX 通知空实现，增加最终 ELF 强符号回归。完整 INS 负载下合计 6000/8000 帧/s 各 10 秒全部交付，整板 CPU 76.77%/95.21%；过载出现提交背压。24 V/0.091 A 单点 ADC 均值 24.641 V/0.08986 A，电压偏高待核对。23 套主机、父项目矩阵、Debug/Release 通过，恢复正常固件并释放探针；见 [CAN 历史验证](Source/IO/CAN/CHANGELOG.md) 与 [文档历史摘要](Docs/CHANGELOG.md)。

- 2026-09-28：按授权完成当前 SRNOS 固件板测，恢复并保留 Minimal Release，释放探针后完成一分钟五流采集。四次完整生命周期窗口零失败，统计增量约 0.99 个百分点、Monitor 约 0.0492%、最长监测临界区 2 µs；ADC 软件注入及 CDC 背压/DTR 重连通过。温稳反复退出、统计余量小列为优化线索，实际 CAN/串口/仪表精度仍待验收。更新根接入状态与底层开发清单。
- 板测工程将原生内核/ADC 适配独立为 OBJECT 目标，父应用保持公共 include 隔离。未修改生产运行逻辑、生成边界、线程预算或内存布局。

- 本轮最终验证：23 套主机回归、87 个公共头/151 单元及闭包、十个父项目固件、13 条应用编译命令隔离、110 个 SDK 对象符号、Debug/Release 和结构/第三方哈希通过。SPI/I2C/Serial 统一 SRNOS_NO_WAIT/WAIT_FOREVER，同步消费者保证在途 buffer 生命周期；板端性能待测，未自动烧录。

- 2026-09-28：完成自维护模块 SRNOS 命名及公共依赖隔离。删除旧 API/头路径/兼容入口，HostLink 注册上下文不透明，Logging 取消无效 timeout 参数，SPI 设备与 HAL/供应商/worker 接口私有，矩阵改用 SDK 类型。Platform 完整链接能力保留，应用编译不再得到 RTOS/CMSIS/HAL/Board/Source include。
- HostComm/SPI/I2C/Serial/Indicators 按事件及截止时间等待；HostComm RX 每轮最多 1024 B，CAN RX 最多 32 帧并公平续跑，CAN RX/TX、HostComm/HostCmd、Indicators/Monitor 保持分离。HostLink 活动请求 10 项与 4 项/2 秒重放缓存分离，固定持有一份待发送结果，交付回调锁外执行；长命令与断连不重复执行业务。
- SPI/I2C 使用预留—复制—发布槽生命周期，修复复制期间取消确认；Serial 总期限覆盖锁竞争，日志运行期耗尽不退化为直接输出，IMU/INS 遥测改逐条短快照。保持线协议、EEPROM 格式、采样频率、硬件时序、线程优先级和默认栈。本轮没有烧录，开销与真实负载验收待完成。

- 2026-09-28：补齐父项目线程建立与管理文档：两种创建路径、句柄/参数所有权、初始化状态、协作回收、延时/通知/同步；同步根 README、Kernel、Minimal、开发约定与 AGENTS。仅文档变更，未改变运行行为或烧录。 文档中根 C++17 / Kernel C11 示例语法及本地链接检查通过。

- 2026-09-28：完成托管任务/SystemMonitor 板端验证；修复 ADC BDMA READY/EN 停止确认、主机 SAMPLE 消息号；优化循环统计热点并新增生命周期/CPU 对照/CDC/ADC 注入板测。

- 2026-09-28：父项目线程合同从 entry 切换为 init/step/cleanup；增加不透明生命周期句柄、协作停止/删除、时间/同步接口及默认 32 KiB 有界池。全部 SDK worker 接入统一外层循环并以 SRNOS_ 命名，保持优先级/栈及私有等待；增加低干扰循环统计、0x0003 遥测和 JSONL，容量 8→12。旧 entry 不兼容；生成代码和第三方内核未改动。验证范围为主机与构建；本次未烧录，时序与开销仍待板测。

- 2026-09-28：SystemMonitor替代Diagnostics线程，1 Hz ADC/任务统计与HostLink发布；增加ADC板级BDMA接入、逐槽统计和主机工具，默认取消扫栈。Indicators/生成文件不变。22组主机、Debug/Release、九个父项目构建及结构检查通过；未烧录/板测。

- 2026-09-28：板级支撑整理为 Support/System、IO、Devices、USB；指示器策略归服务，USB 准备和 MCP 默认配置收拢，蜂鸣器/WS2812 固定硬件宏私有化。保留生成边界及现有时序/调度/内存合同；21 组主机、九个父项目目标、Minimal/Board/EEPROMBringUp 构建通过，未生成/烧录/板测。

### 2026-09-28

- 新增 `Board/generate.py` 系统调用入口，动态定位当前 checkout 的 IOC 绝对路径，经 CubeMX `-q` 生成；自动保留 Board 快照、日志和内容变化清单，核对生成成功及三处 I2C 时序。AGENTS.md 固化生成流程与相对路径报错经验；不自动构建或烧录。
- 按用户要求用 STM32CubeMX 6.18.1 `-q` 脚本执行 `project generate`，生成成功；与生成前快照相比，仅 `Board/Core/Src/i2c.c` 的 TIMING 从旧值更新为 `0x10C7182D`，所有 USER CODE 和其余 Board 文件保持一致。新增 Board 生成说明，时序检查覆盖 IOC/生成代码/Glue 三者一致；本轮未烧录。
- 用户要求的全仓库基线已提交为 `ebef330`，随后将 I2C4 提升至 400 kHz Fast mode：IOC 与 Glue 时序同步，生成文件仅 USER CODE 接入，启动检查固定 PLL3R/滤波配置，未执行 CubeMX generate。
- 保留原队列、期限及 EEPROM 32 B 分段；Debug/Release、20 组主机套件、九个父项目构建和时序检查通过。21 项板测通过，数字 SCL 约 370 kHz，整片写含校验 5.12 秒/读 1.03 秒，压力下 LIS约97–99 Hz/SPA约16 Hz，无总线错误或超时。验证应用新增显式保留数据命令，测试后 EEPROM 与固件未恢复，MCU 继续运行；模拟边沿、掉电/断线未验收。
- I2C4 原回调线程改为 High6/3 KiB 唯一分发者：LIS/SPA 保留槽、后台两槽、周期优先与后台防饥饿、64 B 事务上限、排队/活动期限、完成信号量及按请求取消。启动同步预算增加 512 B，未增加生产线程或改变 IOC/MPU/链接段。
- FT24C128A 读/写/校验统一最多 32 B，保留 64 B 页边界；LIS/SPA 改用 30 ms 排队完成等待。20 组主机套件、九个父项目构建及 Debug/Release 通过；32.864 秒整片并发板测 20 项通过并恢复，LIS约88–93 Hz/SPA约16 Hz，无离线或总线错误。模块 README/CHANGELOG 已同步。

- FT24C128A 增加独立 `SRNOS_EEPROM*` 阻塞/非阻塞状态机：16 KiB、两字节地址、64 B 页边界、分段读写、写周期后逐页读回校验、超时及设备级取消。无新增常驻线程或驱动堆分配；对应版本的参数/HostLink/INS 存储默认关闭。
- 增加 EEPROMBringUp、模块 README/CHANGELOG 和 ASan/UBSan 主机测试；20 组主机套件、专用 Debug/Release 构建通过。板端 20 项检查通过，整片恢复后与外部备份逐字节一致，测试前完整 Flash 已恢复并比对一致。记录现有 LIS/SPA 将总线 BUSY 判为离线的共享访问限制，测试后恢复 READY。

- 补充 `Source/Services/INS/README.md` 与模块 `CHANGELOG.md`；主机保存工具增加 `--streams`，支持双 IMU、INS、磁场、气压全量 CSV、INS/温控状态和 epoch 变化检查，旧默认命令及双 IMU CSV 列保持兼容。主机模拟串口/黄金向量回归通过，本次文档与采集工具更新未改固件、未操作实板。

- 默认提供正式 INS：固定传感器到板内旋转、冻结并归一化的业务安装四元数、单一Mahony双六轴输入及单路降级/预测/恢复、启动探测/温稳/低动态观测/收敛，多读者一致姿态/原始SI数据/状态快照。磁力计和气压计仅发布测量，不参与融合。新增 High5/3KiB/1ms INS worker，不改变 SPI/MCP 所有权。
- 统一温控默认60°C、可配置20..75°C、LSM优先/BMI回退；低于目标10°C满功率，附近PI40/2/0及输出跟踪/限速；任一健康六轴80°C立即关断，触发来源重新有效且全部有效温度低于75°C五秒后恢复。全部六轴失效停止加热；硬故障不自动解除。
- 删除旧全局INS、方向预设、旧融合/校准/存储worker、两套旧温控和BMI驱动简易校准/阻塞兼容入口，不保留legacy选择。默认参数加载与HostLink存储命令关闭，EEPROM驱动本轮未适配。
- BMI DMA完成通知原采样线程当周期发布，去掉额外1ms延迟；最终板测双路时间差最大76µs，双路等权融合。低动态波动明确采用三轴分量整体RMS，不增加滤波或自动校准。
- 新增INSBringUp与0x0102/0103/0104姿态/磁场/气压流、INS状态查询和CLI可选订阅；沿用有界FIFO/丢新计数/会话清理。原0x0100/0101、双IMU保存脚本及CSV格式保持兼容。三个BringUp保留启动音和1Hz心跳，SensorBringUp仅禁用融合，使用同一温控。

验证：19组主机套件、Debug/Release、Board独立入口、三个BringUp、九种父项目固件与链接检查通过。最终实板一次软件复位后收敛，五流同时订阅的三个10秒窗姿态计算均1000Hz、接收1000.4/1000.0/1000.2Hz，无协议错误/缺序/溢出，全部双路等权；取消/DTR重连通过，温控反馈LSM约60°C。十分钟长测由用户执行，未进行精度校准或真实CAN负载验收。没有提交、推送或修改CubeMX生成边界。

### 2026-09-27

- RAW 加热改为 LSM 优先、BMI 回退；LSM恢复健康100 ms后切回，切换清积分并从零缓升，两路失效或任一有效温度达到70°C锁存关闭。使用连续位置式PI 40/2/0、积分上限350/输出500、5个百分点/秒的功率斜率限制，取消旧RAW预热全功率及越过目标清积分行为。新增只读温控状态快照与CDC0每秒来源/温度/占空比日志；legacy温控默认行为保留。目标仍60°C，作用于实际反馈源，参数是首次平滑整定值而非最终温稳保证。

- SensorBringUp 的 RAW 采样新增显式 BMI 60°C 加热目标；默认 RAW 配置仍关闭加热，融合/用户校准/EEPROM 路径保持关闭。新鲜温度驱动原有 PWM/PID，失效、超时或超温锁存关闭；PWM 启动失败同样锁存故障。CLI 默认显示双 IMU 温度与易读采集状态，`--json` 可切回机器输出，保存格式不变。

- 新增主机端 `imu_capture.py`：复用现有解码器保存双 IMU 全量 CSV、原始 CDC1 字节和状态/元数据，支持预热分段、定时结束、Ctrl-C 保存、断连标记与拒绝覆盖。模拟串口验证保存行数与原始字节重新解码一致，并覆盖序号环绕、缺口、重复、时间戳精度和异常收尾；未操作板子或重新采集，未改变固件。

- 将 BringUp 调度后的黄色 LED 心跳从 0.5 Hz 加快至 1 Hz（亮/灭各 500 ms）；仅运行指示器主机回归，未重新烧录。

#### HostLink 双 IMU 遥测与 BringUp 指示

- 修复响应重发和日志入队中的指针容量错误；补全 COBS 超长/截断/重复分隔符恢复、FIFO 排序与容量限制、会话 epoch、DTR/断连清理及 USB 在途缓冲生命周期。原协议、服务编号和旧遥测最新值策略保留。
- 新增启动冻结的 `RegisterTelemetryEx`、传感器发布观察回调、可复用 IMUTelemetry 服务：每颗 64 条静态缓存、FIFO 最多八个发送槽、每包最多六条显式小端 72 B 记录。CDC1 按订阅输出双 IMU 原始/物理加速度、角速度、温度、序号和时间；状态接口报告健康与各类丢弃计数。
- SensorBringUp 开启 CDC1 遥测，保留 CDC0 每秒摘要，禁用 HostLink LOAD/SAVE 与周期栈扫描；新增 Python CLI、黄金向量和使用文档。存储驱动/格式未改。
- BringUp 与 SensorBringUp 显式禁用加热；legacy 初始化、温度等待及恢复遵守冻结开关，普通 SDK 默认行为保留。调度前一次黄色首灯与三段启动音，原 Indicators 线程独立于 CAN/USB 屏障输出 0.5 Hz 黄色心跳，不新增线程。

验证：18 组主机套件、构建矩阵与父项目/链接检查通过。最终 SensorBringUp 实板两个正常 10 秒窗口及软件复位后窗口均约 1 kHz，无协议错误/序号缺口/遥测丢样；取消订阅、DTR 重连及暂停读取后恢复通过。此次暂停读取未触发设备满队列。用户自行进行十分钟长测，实际声光未现场确认。未修改 IOC/生成代码、未提交或推送。

### 2026-09-26

#### 2613 传感器驱动与共享总线

- 新增 HAL-free `Devices/Sensors.h`、初始化冻结配置及 LSM6DS3TR-C/LIS2MDL/SPA06-003 驱动：ID 检查、逐步配置回读、断连重试、原始与物理值一致快照、状态和时序统计。SPA 包含全部工厂系数及压力/温度补偿；不接入融合或安装坐标标定。
- BMI 增加 RAW 路径，避开融合、加热、用户校准与 EEPROM；发布和下一轮发起在同一周期完成，修复原交替调用约 500 Hz 的降频。
- SPI4 改为 High6 唯一分发者、八槽有界队列（四 IMU/四普通）、TX 复制、DMA staging 和最多连续四笔 IMU 后放行普通事务；同步等待、取消、队列期限、活动超时、确认 abort 与迟到 IRQ 隔离统一管理。LSM 固定 mode 3，BMI/MCP mode 0，均为 10 MHz。
- I²C 完成状态改为设备所有，避免共享总线覆盖等待结果；恢复须确认停止，失败隔离总线。新增 LSM High5/2 KiB 和环境 Normal/2 KiB 线程，计入启动预算；DMA/BDMA 布局继续在 D2/D3。
- 修复 ELF 静态检查的 objcopy 调用，显式使用临时输出，避免检查重写原始 ELF 容器。
- 新增 SensorBringUp、10 秒样本/轮询窗口和主机端 GDB 栈导出工具。该配置禁用参数加载、HostLink 存储入口和周期全任务栈扫描，MCP 只被动运行。生成代码、第三方版本、EEPROM/ADC/PHY 未改。

验证：16 组主机套件通过；根 Debug/Release、Board Debug、SensorBringUp Debug/Release、八种父项目固件及内存/容量/来源检查通过。板端 LIS=0x40、SPA=0x11 并持续更新；LSM 默认读 0x00，上拉 MISO 后读 0xFF，仍未通过可达性验证。不能据此宣称 LSM、双 IMU 实时性、传感器精度或真实 CAN 帧压力已验收。没有自动提交。

#### 启动屏障修复后的板端复测

- 经用户授权使用 J-Link V8.96 + ARM GDB 连接；板上向量表及代码段匹配修复版 BringUp ELF，未重新编译或烧录。
- 初次连接时已运行约 634 秒/127 次采样；继续后 648 秒/130 次。软件复位后在 40 秒/8 次及 99 秒/20 次采样，均未复现旧断言，IMU 数据新鲜，当前 Fault 标记为零。
- 记录 CAN RX 最小空闲栈仅 412 B、USB 尚无枚举证据，保留为后续验收项。未做 CAN 帧压力或完整实时性验证。
- 清除本次新增断点后让 MCU 继续运行，保留 GDB/J-Link 会话。

#### 调试协作约定

- 在 `AGENTS.md` 补充 CLion MCP 与 Agent 直接使用 J-Link/GDB 的接入检查、单一会话所有权、暂停现场保护、ELF 核对、有界栈回溯及调试结束状态说明。
- 记录本机实测的 MCP 端口变化、J-Link 旧版符号链接及未知帮助参数仍进入连接流程的注意事项；明确这些路径和版本须重新核对。

验证：仅检查文档变更；未操作调试会话、修改固件、编译、烧录或复位。

#### 修复启动事件屏障误判

- 根据用户 J-Link/GDB 板端记录，系统已进入 RUNNING 并完成一次 BringUp 采样，但约 464 ms 时 Indicators 的等待返回 `0x3`，旧代码以 `== 0x2` 判断失败并进入 Panic/复位循环。
- 修复系统线程和应用声明线程两处等待：先检查 `osFlagsError`，再以掩码确认所需位全部置位。保留 WaitAll、NoClear 和无限等待，不更改线程优先级、启动里程碑或生成代码。
- 新增 Kernel 主机回归，使用真实 CMSIS 声明，模拟合并就绪位、缺位和错误返回，并检查等待参数与单次调用。纳入统一主机测试入口。

验证：13 组主机套件和结构检查通过；修复版 BringUp 的 Debug/Release 编译链接、基础 ELF 内存/TLS/异常向量检查通过。

硬件记录证明的是修复前的失效原因，修复后的板端验证仍待执行；本次不操作用户保留的 GDB/J-Link 会话，不烧录、复位或继续 MCU。修复固件使用独立构建目录，保留原调试 ELF。

#### BringUp 观测应用

- 增加 `Docs/Development.md`，区分离线检查与板端运行验收，列出启动/内存、固定硬件、串口协议、双 CDC/参数和父项目 API 收尾顺序。
- 新增可选 `Examples/BringUp`，在现有应用线程中每 5 秒采集启动状态、heap_5 余量、任务栈高水位、IMU 快照年龄和日志统计，提供调试符号与 CDC0 日志摘要。关闭本示例的启动业务参数恢复，保留固定服务默认行为。
- 根入口通过 `SIRINEMBEDOS_EXAMPLE=Minimal|BringUp` 选择应用，默认保持 Minimal；父项目构建回归加入 BringUp，第七种目标覆盖示例接入。同步 README、AGENTS 和示例说明。

验证：BringUp 独立 Debug 编译及基础 ELF 检查通过；默认 Minimal Debug 重配/构建通过且镜像资源保持原值；七种父项目目标、MSP/libc 覆盖与越界拒绝、结构检查通过。未重复运行未改动组件的 12 组主机套件，其前次结果见下文。

限制：未烧录或实测。全任务栈扫描会挂起调度器，适合低频资源观察，不能作为实时压力验证；`RUNNING`、USB 启动和 IMU 数据新鲜均不等于整板健康。SDK 默认 IMU 温控/校准行为继续保留。

#### 启动配置与资源冻结

- 公共基础操作及启动入口统一为 `SRNOS_*` / `SRNOS::`，应用使用 `SRNOS_ApplicationConfigure` 和 `SRNOS_ApplicationMain`。应用算法、控制状态机、业务线程和具体设备组合仍由父项目管理。
- 增加 `SRNOS_SystemConfig` 声明、冲突和启动预算校验、统一冻结及应用线程装配。CAN/UART/IMU/服务/线程等结构配置初始化后不可重载；服务注册在内核初始化前完成，资源与硬件启动按内核阶段执行。应用线程等待参数加载尝试及 USB 启动阶段结束；启动状态与具体设备健康分别观测。
- 整合三路原生 CAN、外置 CAN、SPI/I2C、IMU/EEPROM、板级指示器和诊断。默认集成硬件访问能力，不自动执行电机使能或业务运动。
- Serial 成为 UART 的唯一 HAL/DMA/回调所有者，协议服务和原始通道通过初始化声明获得独占资源；普通 UART 默认未绑定。DR16、VT03、裁判、H12H、日志按通道拆分，UART7 保持固定 RS485 总线。端口参数、owner 和缓冲预算冻结，错误恢复不改变用途。
- 正常日志改用 Serial/USB sink，不再复位 USART1 或调用 Emergency。调度前日志仅捕获到有界 RAM 启动缓冲，避免触发 FreeRTOS 尚未就绪的临界区；stdio 明确提供正常输出和失败返回策略。
- 参数注册表初始化后冻结，默认只读，只有显式 `SRNOS_PARAMETERS_MUTABLE_BUSINESS` 的业务值可远程写入；系统结构配置不进入主机可写表。新增受锁保护的标量和一致快照、先校验后原子应用的批量写入。持久化加载完整验证后仅更新可写业务值，失败保留当前值；默认启动加载先于 USB 接收主机命令。
- 集成 AL94 USB Composite **1.0.3** 的 Core/COMPOSITE/CDC_ACM，CDC0 用于日志、CDC1 用于 HostLink；保留 vendor 原文、许可和哈希，原 descriptor 补丁由构建目录派生。SDK glue 补齐 PA11/PA12，复用生成的 PCD。CDC class 使用静态存储，移除未使用的 libc 分配别名，不增加其他 USB class。
- Fault 向量、retained record、HSKP/Debug Watch 和 MPU/Cache 专用段接入 SDK；基础 API 更名不改变 Fault v2 二进制布局。默认紧急输出只保留 RAM 记录，可在初始化时显式预留；不自动占用串口。
- 最终链接采用 `Board/Linker/SirinEmbedOS.ld`，普通 data/TLS/BSS 放 D1，DTCM 保留热数据、RTOS heap 和 MSP；加入 D2 DMA/Watch/retained、D3 BDMA、ITCM 复制和独立 libc arena。Picolibc 静态递归锁接入 FreeRTOS，IRQ 不允许动态分配；生成基线文件保留原位置。
- 新增 `Examples/Parameters` 业务 PID 增益和快照示例、`Examples/PrototypeBindings` 串口用途示例；重写 README 和 AGENTS 的当前能力、父项目接入、初始化冻结与验证边界。

验证：12组主机套件通过；根与Board独立Debug/Release均链接通过；外部父项目6种固件（含无GC全组件、94公共头）通过。检查MSP/libc容量覆盖与越界拒绝、DMA银行、TLS及真实异常向量。不将旧基线的 size 数据用于本轮资源预算。

限制：未烧录，USB 枚举、UART DMA/IDLE/错误恢复、RS485 收发、CAN、IMU、FPU/TLS、缓存一致性和 Fault 仍需板端验证。默认初始化不代表外接设备存在或服务健康，具体业务必须检查相应状态。

### 2026-09-25

#### SDK 构建与 FreeRTOS 配置

- 根目录改为可独立构建、也可由父项目 `add_subdirectory()` 引入的 SDK，提供 `SirinEmbedOS::Platform`、Board/Kernel 组成目标和 `sirinos_add_firmware()`。父项目决定工程名、固件目标和应用源码，SDK 作为子目录时不自动创建示例固件。
- `Board/CMakeLists.txt` 将生成清单接入对象库，在局部作用域隔离 CubeMX 对 `CMAKE_PROJECT_NAME` 的依赖；Board 独立入口复用同一 SDK，生成清单、基础链接脚本和启动汇编保持原样。
- ST Arm Clang 工具链适配将链接脚本与 map 从生成的全局参数移到 SDK/固件 target。Board preset 改用同一适配工具链；旧 Board 缓存需刷新，否则会继续加载原工具链。配置阶段检测该情况并给出清理提示。
- 固定 FreeRTOS **10.6.2**、CMSIS-RTOS2 wrapper/API、CM4F 浮点 port 和 heap_5；第三方文件保持字节一致，附来源、参考 commit、许可证和 SHA256 清单，没有升级依赖或引入嵌套子模块。
- 新增 SDK 管理的 FreeRTOS 配置、Kernel 启动和基础失败 hooks，以及 `Board/Glue` 接线。生成 `main()` 的 USER CODE 在 HAL 初始化后调用启动入口，SDK 启动唯一一次调度器，在 Application 线程调用父项目的 `OS_ApplicationMain()`；应用入口可创建线程或返回。
- 保留 1 kHz Kernel tick、TIM7 HAL tick、56 级线程优先级及 IRQ syscall 门限 5；SVC/PendSV 由 port 实现，SysTick 由 CMSIS wrapper 实现。启用 Picolibc 的每任务 TLS，关闭旧 Newlib reentrancy。
- 使用自维护链接片段保留 DTCM 64 KiB + AXI SRAM 128 KiB heap_5；两区均为 NOLOAD，heap_5 在内核初始化时注册并初始化管理结构，payload 不保证清零。链接检查 SRAM 边界，并确保 libc 的 `_end` 位于 DTCM RTOS heap 之后。
- 增加 `SIRINEMBEDOS_CONFIG_HEADER` 接口，父项目可统一覆盖初始应用线程栈（默认 4096 字节）、CMSIS 优先级（默认 24）和堆容量；同一构建内 SDK 使用一套配置。
- 增加最小 heartbeat 应用、接入 README、父项目 CMake/C++/TLS 构建回归，并同步 AGENTS 的已实现范围。

验证：ST Arm Clang 21.1.1 / Picolibc 下，根目录 Debug、Release 与 Board 独立入口均编译链接通过。Debug Flash **60,044 B**，Release Flash **47,340 B**；两者 DTCM **76,224 B**（含 64 KiB RTOS heap 与生成脚本的 heap/stack 预留）、D1 **131,072 B**。这些数字不包含应用后续增加的资源。

构建检查通过：第三方哈希、含空格的父项目路径、父工程名保持、两个不同固件名、helper 与直接链接 Platform 两种接入、应用配置覆盖、C++ 构造/TLS 段、强 IRQ/hook 符号和 NOLOAD heap 布局均完成静态检查。生成边界另行核对：只有 `main.c` 的 USER CODE 区、项目维护的 Board 入口和 preset 发生修改，IOC 未改、未重新 generate。

限制：尚未烧录；实际调度、FPU 上下文、TLS 隔离与外设行为仍需板端验证。CMSIS 原有 `optimize("Os")` 被 Clang 忽略的警告仍在。完整 Fault/Debug Watch、运行统计、DMA/BDMA 段、libc malloc/new/stdio 的 RTOS 锁、其他 middleware 和系统服务不在该次验证范围内。普通 heap_5 分配不能作为 DMA 缓冲使用。

#### 早前工程记录

- 补充 `AGENTS.md` 中 SDK、Board 与父项目的目录层级用途表、代码放置和依赖规则，明确固定硬件能力、系统服务与应用算法的归属；仅更新规划文档，未创建空壳目录或修改固件。
- 修复 CLion 普通配置仅选择 ST Arm Clang、未加载 CMake 工具链时被当作 macOS 工程的问题：根入口在 `project()` 前为独立构建设置默认板级工具链；保留显式工具链和父项目的控制权。不修改 CubeMX 生成内容。
- 使用 CLion 自带 CMake 4.3 / Ninja，在 `cmake-build-debug-starmclang` 刷新缓存，模拟不传工具链的 Debug 配置；配置、编译和链接通过，生成 `SirinEmbedOS.elf`。Flash 49,136 B、DTCM 7,216 B；仍有已有 CMSIS `optimize` 属性警告。未烧录或验证板端运行。

### 2026-09-24

#### 工程与架构

- 新增 SDK 根目录 CMake 入口、Debug/Release presets 和 ST Arm Clang 工具链路径适配，CLion 可直接打开 `SirinEmbedOS/`；复用 Board 生成源码清单，不修改生成文件。根目录构建目标为 `SirinEmbedOS.elf`，新增 IDE/构建输出忽略规则。
- 在 `AGENTS.md` 明确单一 SDK 子模块的组织方式：SDK 提供固定硬件能力和公共服务，父项目管理特有控制算法、业务任务与配置。
- 写入 SDK 和父项目的目录规划、CubeMX 生成边界、CMake 接入与启动所有权规则。目录按需建立。
- 明确 CAN、板载 IMU 等固定能力默认集成，UART 等可变用途资源保留配置和占用管理空间。

#### HAL 生成基线

- 板级配置位于 `Board/OMNICTL2_3.ioc`，CubeMX 生成内容集中在 `Board/`。
- 生成目标使用 CMake 与 ST Arm Clang；当前工具链配置为 `STARM_PICOLIBC`。
- 生成的构建清单未引入 FreeRTOS、FatFs、LwIP 或 USB Device 协议栈，后续由 SDK 独立接管。
- 保留 TIM7 作为 HAL 时基；生成的 C 中断文件未实现 SVC、PendSV、SysTick，为后续 Kernel 接入保留入口。
- ADC3 规则转换序列扩展为 5 个通道：IN10、IN0、温度传感器、VREFINT、VBAT；采样时间均为 640.5 cycles。
- 已确认 ADC3 生成配置使用软件触发、DMA One-shot、BDMA Channel2、半字传输及 Normal 模式。

#### 验证

- 根目录 Debug 配置与构建通过，使用 ST Arm Clang 21.1.1 / Picolibc。验证时将本机 ST Arm Clang 的 bin 目录加入 PATH；CLion 工具链环境也需要提供此路径。Release preset 已提供，尚未验证。
- 在 `Board/` 执行 `cmake --preset Debug` 和 `cmake --build --preset Debug -j 8`，ST Arm Clang 21.1.1 配置、编译和链接通过。
- 该次链接报告 Flash 占用 49,136 B，DTCM 占用 7,216 B。此数据仅代表当前 HAL 基线，不是完整 SDK 的内存预算。
- 编译出现 ST CMSIS 的 `optimize("Os")` 属性被 Clang 忽略的警告，未阻止链接。
- 已静态核对 ADC3 五通道、TIM7 tick 及外设 HAL 中断调用；未烧录或验证实机运行。

#### 已知限制与待接入项

- HardFault、MemManage、BusFault、UsageFault 仍为 CubeMX 生成的死循环处理函数；接入 SirinOS Fault 前需要统一所有权。
- USB HAL MSP 当前仅生成 PA9 VBUS GPIO 配置，未生成 PA11/PA12 的 GPIO 初始化；后续板级 glue 需要复核并补齐 USB 准备流程。
- 当前基础链接脚本尚未接入 SDK 的 DMA/BDMA 专用内存段；ADC3 BDMA 缓冲的 SRAM4/D3 放置与初始化需要后续实现。
- ETH、SD 初始化函数已生成，但未由 `main()` 自动调用；ADC3 尚未执行校准与启动采样。
- 当前 Board CMake 是独立板级固件工程，尚未完成供父项目引入的 SDK target 接入。

## 历史记录归档：原根 README 与 AGENTS

以下为整理前的阶段记录，保留当时的配置、结论与未验边界，不代表当前板上状态；当前用法见 README 与 QuickStart。

已完成主机测试和固件编译链接。用户首次板端调试确认可进入 RUNNING 并采到 IMU 数据，同时发现启动事件屏障误判导致复位循环；源码已修复，随后直接连接及一次软件复位复测未再现旧断言，IMU 数据持续更新；完整外设和压力验收仍待完成。

2026-09-28 全面命名与事件等待改造后已重新烧录：两组统计开关对照、托管任务生命周期、ADC 软件故障注入及 CDC 背压/DTR 重连通过。统计开销约 0.99 个百分点，接近目标边界；当时实际 CAN 负载、串口、仪表精度和长期压力仍未验收。当时的功能缺口与优化顺序见 [开发清单](Docs/CHANGELOG.md#当前底层开发与优化优先级)。

2026-09-28 曾完成 CAN1↔CAN3 外接负载短测并修复通知 weak 链接错误：合计 8000 帧/s 全部交付，但整板 CPU 95.21%，业务余量不足；CAN2/CAN4 与故障恢复仍待测。ADC 24 V 仪表单点读到均值 24.641 V，保持标称倍率待进一步核对。

2026-10-03 按每路 CAN 同时 3 kHz RX＋3 kHz TX 目标优化：P0 CAN1/2/3＋BMI/LSM/INS 为 48，P1 CAN4＋UART 服务为 47，SPI4 共享依赖为 49、事务优先 IMU；直接任务通知、冻结路由少复制、MCP 减少重复 SPI 事务，并修复原生 RX 满环竞争、Bus-Off 接入与电机反馈并发。公共 CAN API、八个收发线程、栈/队列容量保持；电机对象私有布局变化，父项目须重新编译。当时 PCB 离线，仅验证软件及构建。

随后接入四路独立的启动期 Classic/FD 选择，默认仍为 Classic 1 Mbit/s；新增公开 FD 原始订阅/one-shot API，业务协议由应用定义。原生 FDCAN 改用 80 MHz 及按模式检查的固定 Message RAM 窗口，MCP 使用 16 RX＋12 TX 的 64 B FD FIFO；P0/P1保持；CAN4优化后合并为一个4KiB服务，CAN总计七个worker。原生 RX 每轮最多处理 32 帧、每次搬运 4 帧，避免 64 B 帧批次挤占回调栈。FD 配置、长度、资源和离线限制见 [CAN](Source/IO/CAN/README.md)；历史 Classic 实测不能证明新时钟/FD 运行效果。

2026-10-06 已完成当前优化/FD固件的 CAN1↔CAN3 板测：356格式用例通过；Classic8B和FD8M64B各3k收＋3k发持续60秒、零数据错误/接收丢帧，整板CPU32.00%和35.13%，FD5M64B同档10秒通过；FD4M及以下64B同档受带宽/仲裁限制。双IMU/INS保持1kHz；LIS 采样率与身份记录仍有待核对项，四路同步和物理故障恢复尚未验收。本次结束恢复测试前的原固件并完整读回校验，未将测试镜像留板。

2026-10-06优化前的CAN1↔CAN4共享SPI基线：Classic双向各2k时CPU约91.5%，各3k请求因生产周期不足未达标；FD5M及8M对齐测试在64B各3k时出现MCP硬件RX溢出。双IMU/INS保持1kHz。默认FD8M首帧失败，测试中将CAN1仲裁采样点对齐到80%后功能通过，生产配置仍待修正。

2026-10-06 CAN4 单服务与 SPI 序列优化完成；完整测量值、负载条件和恢复范围见 [CAN4 板测摘要](Source/IO/CAN/CHANGELOG.md#can4-20261006)。

CAN adapter 的毫秒时间、发送 TTL 和重试统一使用 `SRNOS_TimeMs()`，设备反馈时间可以直接与应用任务时间比较；不与 HAL/DWT 原点混用。该时间基准修复已在 父项目实测。

2026-09-29 已完成持久化板测，并按实测噪声提高静态校准阈值：连续两次双IMU校准保存成功，软件复位后revision2加载一致、INS未校准标记清除。当前保留新固件及校准记录。真实断电、校准绝对精度仍待验收。

- 2026-09-28 本机 CubeMX 6.18.1 / DB.6.0.181 使用该 CLI 路径成功生成，原始调用仅将 `hi2c4.Init.Timing` 更新为已板测的 `0x10C7182D`，407 个 USER CODE 区域完整保留。实际版本以运行日志为准，macOS bundle 曾显示过期的6.13。后续仍检查实际差异，不假定每次只改一行。

CubeMX 生成、构建与调试会话维护方法见 [Board 指南](Board/README.md)和[开发指南](Docs/Development.md)。

2026-09-26 用户首次板端记录已确认 RUNNING/IMU 初步运行，同时定位到启动事件屏障将成功返回 `0x3` 误判失败而复位。两处等待已改为错误位检查加所需位掩码；13 组主机套件和修复版 Debug/Release 构建通过，修复后板端尚待复测。

18 组主机套件、根 Debug/Release、两个 BringUp、Board 独立入口、八种父项目目标与 SensorBringUp Release 构建已有通过记录。2026-09-27 短测实板 BMI/LSM/LIS/SPA 均 READY，LSM=0x6A、LIS=0x40、SPA=0x11；历史 LSM OFFLINE 不代表当前状态。双 IMU 遥测约 1 kHz，十分钟长测由用户执行，不能据短测宣称完整验收。检查当前会话所有权再操作探针；

## 2026-10-03 CAN Classic / FD 启动合同

FD 每路 32 条路由、64 个 TX 槽，共 23312 B 静态 D1；Classic 16 B 帧和 59920 B ITCM context 保持。原生 RX 每轮最多 32 帧、分小块搬运以保留 4 KiB worker 栈余量；不新增线程或扩堆。Native FDCAN 改用 PLL2Q 80 MHz（P/R 输出不变），三路 Message RAM 固定窗口从 0/0x300/0x600 word 开始；FD 每路 676 words，Classic 每路 232 words。CAN4 FD FIFO 为 RX16/TX12、64 B，共2016 B。软件支持的位速率不代表本板收发器/布线已通过高速实测。优先级仍为 P0 CAN1/2/3+IMU，P1 CAN4/UART；FD 负载必须另按帧长、BRS和位速率验算，不能将 Classic 3 kHz 收发目标外推到所有 FD 配置。2026-10-03离线；2026-10-06 CAN1/3短接实测Classic8B及FD8M64B各3k收发60秒通过，FD5M64B同档10秒通过，FD4M及以下64B该档过载。RX最小栈余3092 B、双IMU/INS连续1kHz；测试后完整恢复原Flash，不应假设当前板上已有FD固件。
