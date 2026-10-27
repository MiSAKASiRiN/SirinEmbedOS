# AGENTS.md

## 平台定位与参考工程

本目录是 **SirinEmbedOS SDK**。历史来源、提交及许可状态见 `Docs/Migration/`；本仓库不依赖个人工作区或内部仓库地址。

SDK 对应用提供统一 API、硬件资源访问与系统服务。控制算法、机构状态机和业务线程属于父项目。

平台使用 FreeRTOS / CMSIS-RTOS2 / heap_5，提供 Core、诊断、CAN/SPI/I2C、固定设备、串口协议、日志、参数、SirinLink、USB 与静态 IPv4/UDP。配置与注册在启动阶段冻结；主机只可修改显式声明的业务数值。接口用法见 [QuickStart](QuickStart.md) 和 [文档导航](Docs/README.md)，构建步骤见 [开发指南](Docs/Development.md)。

复用 prototype 能力时，以设计与实现参考为主，不直接整包搬运代码。两者基于类似的硬件平台，因此原工程的硬件绑定、协议、资源配置和运行经验具有参考价值；具体差异仍以本工程 IOC、实现和已确认硬件资料核对。原工程的架构、目录划分、接口和命名不一定适合 SirinEmbedOS，应按本工程的职责边界与命名规范重新判断，必要时重构或重新实现。

未经明确要求，不修改参考工程。按本次任务需要读取相关模块，不整包复制旧工程，也不把旧工程历史结构直接当成新架构要求。

## 仓库与目录规划

本仓库直接作为 **SirinEmbedOS SDK 仓库**，其他应用工程通过一个 Git 子模块引入。SDK 内整合自维护模块，不继续沿用原工程几十个 Framework 子模块的组织方式，也不通过嵌套子模块把同样的碎片化结构藏到内部。第三方代码固定版本随 SDK 管理，保留来源、许可证和补丁记录。

源码分发不包含测试套件、专用 BringUp 固件或原始现场资料。测试套件及其 BringUp 依赖可保留在本地，但必须被 `.gitignore` 忽略且从 Git 索引取消跟踪，不使用强制添加绕过此规则，也不作为公开 SDK 的默认构建依赖。API 用法示例与生产诊断接口保留。

目录按实际能力维护，不预建空壳模块：

```text
SirinEmbedOS/
├── AGENTS.md
├── README.md
├── CHANGELOG.md               # 修改历史、验证结果与待验事项
├── QuickStart.md              # 构建、父项目接入与完整示例
├── CMakeLists.txt              # SDK 构建入口，对父项目导出组件
├── Include/SirinEmbedOS/       # 对应用公开的 C / C++ 头文件
│   ├── Kernel/
│   ├── IO/
│   ├── Services/
│   └── Diagnostics/
├── Source/
│   ├── Core/                  # 基础操作、Fault、公共类型
│   ├── Kernel/                # FreeRTOS 封装、hooks、线程与同步
│   ├── IO/                    # CAN、UART、SPI、I2C 资源与访问
│   ├── Devices/               # IMU、EEPROM、外置 CAN 等设备驱动
│   ├── Services/              # 日志、参数、SirinLink、USB、网络、文件系统
│   └── Diagnostics/           # Debug Watch、运行统计、健康状态
├── Board/                     # 当前唯一目标硬件，CubeMX 输出边界
│   ├── OMNICTL2_3.ioc
│   ├── Core/                  # CubeMX 生成的 main、外设初始化、中断入口
│   ├── Drivers/               # CubeMX HAL/CMSIS
│   ├── CMakeLists.txt          # 项目维护的板级组件/独立构建入口
│   ├── CMakePresets.json
│   ├── cmake/                 # CubeMX 生成的工具链、HAL 源文件清单
│   ├── Support/                  # 自维护的板级接线、回调与启动适配
│   └── Linker/                # 自维护的内存段扩展及链接配置
├── ThirdParty/
│   ├── FreeRTOS/
│   ├── FatFs/
│   ├── LwIP/
│   └── USBDevice/
├── Config/                    # SDK 默认配置及配置规则
├── CMake/                     # 自维护的组件选择、配置生成与构建辅助
├── Tools/                     # Panic 解码等配套工具
├── Docs/                      # 使用指南、来源索引及验证方法
└── Examples/
    ├── Minimal/               # SDK 自身可构建的最小应用
    ├── Parameters/            # 显式业务参数及一致快照
    └── PrototypeBindings/     # 通过公开 API 声明原串口用途
```

- 自维护 C 函数、类型和跨文件符号使用 `SRNOS_<能力><动作或类型>`；枚举、配置宏、头保护使用 `SRNOS_<MODULE>_*`，C++ 位于 `SRNOS` 下的能力命名空间。旧 API/头路径不保留兼容别名。仅第三方、CubeMX 和规定的 RTOS/HAL/USB/供应商 ABI 入口保留原名；保留来源身份、上游版本与许可证；公开内容不得包含个人路径、内部端点或原始调试转储。
- 父项目仅包含 SDK 公共头。`SirinEmbedOS::Platform` 提供完整链接但不传播 RTOS/CMSIS/HAL/Board/Source include；私有依赖只授予 SDK 实现。新增公共头须通过独立 C11/C++17、依赖闭包与命名检查。迁移合同见 `Docs/SDKMigration.md`。
- 公共头文件集中在 `Include/SirinEmbedOS/`；私有头文件放实现旁边，不要求每个小模块再套 `Inc/Src`。
- `Board/` 暂不增加多厂商、多 MCU、多板型目录层级；出现真实硬件差异后再调整。
- CubeMX 当前还在 `Board/` 根生成启动汇编、基础链接脚本等文件，保留其生成位置。最终固件使用自维护的 `Board/Linker/SirinEmbedOS.ld`；不通过搬动生成文件改变 regenerate 的输出边界。
- `Board/Core/` 是生成代码，`Source/Core/` 是 SDK 基础能力，两者职责不同。
- 本文中的 API 分组与目录不表示对应实现已经存在；不要仅按目录名推断功能可用。

### 目录分级与用途

目录分级用于确定代码归属，不要求每层都有独立线程、静态库或运行时对象。

| 层级 | 目录 | 用途与边界 |
|---|---|---|
| 对外接口 | `Include/SirinEmbedOS/` | 应用可包含的公共声明、类型和配置合同。按能力分组；基础公共头可直接放在该目录，按实际规模再细分。不要公开内部 context 或让应用依赖私有头文件路径。 |
| 基础能力 | `Source/Core/` | 字节操作、边界检查、Endian、公共类型、Assert/Fault 等。不得依赖日志任务、业务任务或具体设备服务；Fault 的硬件捕获与紧急输出通过板级适配完成。 |
| 内核接入 | `Source/Kernel/` | FreeRTOS 封装、线程与同步原语、调度接入、内核 hooks 和资源管理。FreeRTOS 上游实现放 `ThirdParty/FreeRTOS/`，不与封装混放。 |
| 硬件通路 | `Source/IO/` | CAN router、UART 占用与用途管理、SPI/I2C 事务、异步收发及并发控制。管理硬件通路，不承担机器人控制策略。 |
| 设备能力 | `Source/Devices/` | 固定板载设备的寄存器、协议、初始化、采样与状态处理，例如 IMU、EEPROM、外置 CAN 控制器。通常通过 IO 使用总线；作为 IO backend 的设备驱动应拆清调用方向，避免循环依赖。 |
| 系统服务 | `Source/Services/` | 日志、参数、SirinLink、USB、网络、文件系统等长期运行或供应用调用的功能。服务的 worker/task 与该服务放在一起，不恢复一个平铺全部 `tskptt_*` 的目录。 |
| 系统观测 | `Source/Diagnostics/` | Debug Watch、线程运行统计、健康状态与诊断快照。区分低成本观测基础设施和主动采样任务；底层模块不得为了记录计数而依赖上层通信或日志服务。 |
| 固定板级实现 | `Board/` | 当前硬件的 IOC、生成代码和自维护接线。允许固化本板引脚、实例和绑定，不为假想板卡建立额外平台层。 |
| 上游依赖 | `ThirdParty/` | 固定版本的 RTOS 和 middleware 源码。保留许可证和来源；本项目的适配优先放 `Source/` 或 `Board/Support/`，必须修改上游时记录补丁。 |
| SDK 默认配置 | `Config/` | 公共能力的默认容量、服务策略、配置声明和校验规则。父项目可以通过明确接口覆盖允许变化的项目，不把某台机器人的参数写成 SDK 默认硬件事实。 |
| 构建装配 | `CMake/` | 自维护的工具链适配、组件依赖、配置生成和固件构建辅助。与 CubeMX 管理的 `Board/cmake/` 区分。 |
| 开发工具 | `Tools/` | 在开发机执行的 Panic 解码、检查或辅助脚本，不参与 MCU 固件源码扫描。 |
| 接入示例 | `Examples/` | 通过公开 API 使用 SDK 的应用示例；`Minimal/` 验证最小启动与接入合同，不复制 SDK 内部实现。 |

`Board/` 内进一步按所有权划分：

| 目录或文件 | 用途 |
|---|---|
| `OMNICTL2_3.ioc` | 当前硬件配置输入；改动后核对引脚、时钟、DMA、NVIC 和生成差异。 |
| `Core/Inc`、`Core/Src` | CubeMX 生成的 HAL 初始化、main 和中断入口；自维护接入仅使用 `USER CODE` 区。 |
| `Drivers/` | CubeMX 配套的 HAL/CMSIS，通常不直接修改。 |
| `Support/` | 自维护的 System/IO/Devices/USB 四组板级支撑；包含启动、Fault/libc、硬件绑定、时序和回调，服务行为仍归 Source/Services。 |
| `Linker/` | 自维护的完整固件链接脚本，包含 DMA、BDMA、保留区、heap、MSP 和 libc arena；原生成脚本保留作生成基线，不叠加参与最终链接。 |
| `cmake/`、生成的启动文件和基础链接脚本 | 保留 CubeMX 的生成位置与管理方式，经项目侧适配接入构建。 |

### 放置与依赖判断

- 应用调用 SDK 公共接口；SDK 不反向包含父项目的任务或控制算法头文件。父项目通过配置、回调或任务描述显式接入。
- 常规调用关系为：系统服务使用 Kernel、IO 或设备能力，设备驱动使用所需 IO；这些实现都可以使用 Core。固定硬件绑定通过 `Board/Support/` 装配。
- Support 可以引用两端接口来完成注册与回调转接，但不要把它变成跨层共享业务逻辑的存放处。
- 不为了目录分层给每次函数调用增加虚函数或动态注册。固定硬件允许静态绑定；依赖清晰与可复用接口优先。
- CAN 的队列、路由和发送调度放 `Source/IO/`；具体器件寄存器实现放 `Source/Devices/`；HAL 实例和 IRQ 接线放 `Board/Support/`。必要时用小型 backend 接口隔开，不能让 router 与器件驱动互相依赖。
- IMU 的寄存器操作、采样和通用设备状态放 `Source/Devices/`；应用姿态控制、机构补偿和动作策略留在父项目。板载 IMU 默认集成不改变这个边界。
- 一个服务内部的任务循环、队列和私有辅助函数可以放在同一个能力目录。是否继续分子目录取决于实际规模，不机械建立多级 `Inc/Src/Modules/Impl`。
- 新增公共 API 时，同步考虑其实现目录、配置归属和测试位置；目录移动不应顺带改变协议、时序或资源所有权。

### 使用 SDK 的父项目

```text
MyRobot/
├── AGENTS.md
├── CMakeLists.txt              # 最终固件目标和应用构建
├── CMakePresets.json
├── SirinEmbedOS/               # SDK Git 子模块
├── App/
│   ├── app_main.cpp           # 应用入口，通常不是另一个 C main()
│   ├── Tasks/                 # 项目任务、线程描述与配置
│   ├── Control/               # 项目特有的控制算法
│   ├── Behavior/              # 模式、状态机、业务流程
│   └── Devices/               # 项目外接设备及协议扩展
├── Config/
│   ├── system_config.*        # 系统服务选择和资源预算
│   ├── uart_config.*          # UART 用途与参数
│   └── robot_config.*         # 电机 ID、机构参数、标定等
└── build/
```

配置文件名是示意，具体格式随接口设计确定。父项目管理项目特有控制算法、业务任务、外接设备组合和配置；不重复维护一套 HAL/middleware，也不通过修改 SDK 子模块来选择 UART 用途或添加任务。

父项目内部职责如下：

| 目录 | 用途 |
|---|---|
| `App/Tasks/` | 应用线程入口、周期循环、线程描述和业务编排，通过 SDK 访问资源。 |
| `App/Control/` | 项目特有的控制器、解算、观测器和算法组合；尽量将数学逻辑与任务调度、硬件访问分开。 |
| `App/Behavior/` | 操作模式、状态机、动作流程、应用级安全条件与控制目标选择。 |
| `App/Devices/` | 本项目独有的外接设备和协议扩展；使用 SDK 提供的总线接口，不私自争用 HAL 资源。 |
| `Config/` | 本项目的 UART 用途、系统资源预算、外设组合、电机 ID、机构与标定参数。与 SDK 的 `Config/` 默认配置区分。 |
| `build/` | 编译生成物，不作为源码维护。 |

SDK 默认提供本板的 CAN、板载 IMU 等固定能力；CAN 上连接哪些电机、控制目标和周期、IMU 数据如何参与控制，由父项目决定。经多个应用确认有复用价值的算法，再按需求沉淀到 SDK。

### 父项目线程建立与管理

- 父项目新线程使用 `Include/SirinEmbedOS/Kernel/Thread.h` 的 `SRNOS_ThreadConfig`（init/step/cleanup），一般业务代码不直接调用 FreeRTOS/CMSIS 创建、删除、通知或延时 API。同步对象使用 `Sync.h`，统计 hook 由 SDK 内部提供；旧 entry 合同不保留。
- 需要保存管理句柄的任务，优先在 `SRNOS_ApplicationMain()` 或已有应用任务中调用 ThreadCreate；先准备参数/同步对象，检查创建结果，再启用生产者。ApplicationMain 可返回，创建的业务线程继续运行。不要在周期 step 中再写永久循环或周期 Delay。
- 固定任务可以在 ApplicationConfigure 中填写 threads[]/thread_count；回调内只声明、不创建。名称和参数不得指向配置回调的临时对象。列表任务可能先于 ApplicationMain 运行，不能依赖后者先初始化资源；列表当前不返回句柄，有管理需求优先运行期 Create。
- Create 成功与 init 成功分别通过返回值和 GetInfo 查询。回调可能先于 Create 返回执行，自身句柄用 GetCurrent；业务参数由调用者保留至 Delete 成功。
- 管理者负责 RequestStop/Join/Delete；Delete 已包含请求停止与等待，只有成功后才能清空句柄/回收参数。超时或 BUSY 保留资源稍后重试，正常退出或失败也须 Delete。回调自身结束返回 STEP_STOP/FAILED，不能同步删除自身；SDK 服务不得由父项目删除。
- 调度用 TimeMs/Delay/DelayUntil，DelayUntil 是绝对毫秒截止点、有限跨度不超过 INT32_MAX；TimeUs 是独立观测时钟。ISR 仅使用明确的 FromISR 接口，不能创建/删除或阻塞。
- 应用名称唯一、1～15 字节，禁止 SRNOS_ 前缀；优先级 1～39，栈按字节且包含 TLS。启动列表和运行期对象共用有界池/数量限额，使用 KernelGetResources 检查用量，不能把名义数量上限当成内存保证。
- 具体示例、超时及所有权合同以 [根 README](README.md#托管任务与父项目内核接口)、[Kernel README](Source/Kernel/README.md) 和 [Minimal](Examples/Minimal/README.md) 为准；公共合同变更同步维护这三处及相关 CHANGELOG。

### 构建与启动边界

- CLion 默认打开 SDK 根目录 `SirinEmbedOS/`，不打开 `Board/` 作为日常工程；IDE 配置与构建输出分别留在根目录 `.idea/`、`build/`。忽略规则不阻止 IDE 创建文件，实际位置由打开的工程目录决定。
- 当前根目录将 SDK 默认能力与 Minimal 应用共同构建：`cmake --preset Debug`、`cmake --build --preset Debug`，产物为 `build/Debug/SirinEmbedOS.elf`；Board 独立构建转入同一 SDK，产物为 `Board/build/Debug/sdk/SirinEmbedOS.elf`。
- 根目录 `CMake/starm-clang.cmake` 复用 Board 生成的编译器/ABI/Picolibc 设置，移除生成模板的全局链接脚本与 map 参数，改由 SDK/固件 target 指定。CubeMX 模板变化导致锚点不匹配时配置应失败，需复核后适配。更换旧 Board 工具链缓存后须重新配置；直接使用生成的工具链会被 SDK 拒绝。
- CubeMX 选择 CMake，生成内容集中在 `Board/`。当前板级基线使用 ST Arm Clang，后续以实际工具链配置为准，不照搬旧 GCC 参数。
- SDK 根 `CMakeLists.txt` 已导出 `SirinEmbedOS::Platform`，父项目可通过 `sirinos_add_firmware()` 或自行创建 executable 并链接该 target 接入。`SirinEmbedOS::Board` 和 `SirinEmbedOS::Kernel` 是内部装配的组成部分，不是独立固件。
- 父项目决定最终固件名、应用源文件和顶层交叉编译工具链。SDK 被 `add_subdirectory()` 引入时不能擅自重置父项目工具链或固件目标。
- SDK 路径基于自身目录解析，不以 `${CMAKE_SOURCE_DIR}` 假定 SDK 就是顶层项目。应用配置显式传入，不反向读取父项目约定目录。
- CubeMX 生成的 Board CMake 按实际内容适配接入，不能假定它天然可作为库；特别检查 `CMAKE_PROJECT_NAME`、固件 target、链接选项和路径。
- SDK 持有唯一的板级 `main()`，在 `USER CODE` 区调用 Board support。父项目在 `SRNOS_ApplicationConfigure(SRNOS_SystemConfig *)` 声明资源用途、服务与应用线程；配置回调、协议/业务参数注册、冲突检查和冻结都在 `osKernelInitialize()` 前完成，禁止在这些回调中阻塞 I/O 或创建线程。可选 `bind_channels` 用于内置协议之后声明原始通道、H12H 和 RS485 设备。
- 父项目实现 `SRNOS_ApplicationMain()`，在调度后的 Application 线程执行，允许返回。SDK 先完成启动期业务参数加载尝试和 USB 启动，再放行该入口及 `threads` 中声明的应用线程；启动参数加载失败使用声明默认值，运行期显式 LOAD 失败保留当前 RAM。父项目不得再次初始化/启动调度器。
- SDK 统一装配总线、IMU、日志、SirinLink、诊断等服务线程。父项目通过 `SIRINEMBEDOS_CONFIG_HEADER` 覆盖 `SRNOS_APP_*`、`SRNOS_HEAP_*` 等编译期默认值，通过系统配置声明线程栈/优先级和启用项；一套构建共享一套 SDK 配置。`SRNOS_SystemHeapBudget()` 校验保守启动预算，不覆盖应用运行期额外分配。
- 默认普通 UART 不绑定协议或日志。`Examples/PrototypeBindings` 显式演示原端口分配，`Examples/Parameters` 演示业务值注册与安全快照。启动阶段状态、服务线程已创建、设备健康是不同事实；USB 已启动不代表枚举成功，CAN4/IMU 的运行就绪需看各自状态。
- `Examples/Minimal` 用于独立验证 SDK，也应使用公开配置与接入机制，避免形成一套仅示例能用的特殊路径。

### CubeMX CLI Generate

- 实际板级目录为 `Board/`。需要生成 HAL 时优先运行 `python3 Board/generate.py`，可从任意工作目录使用该脚本的绝对路径调用；脚本根据自身位置定位当前 checkout，不依赖开发者机器上的固定工程路径。
- 脚本通过系统进程调用 CubeMX `-q <脚本文件>`，命令依次为 `config load "<当前checkout的IOC绝对路径>"`、`project generate`、`exit`，工作目录固定为 `Board/`。macOS CLI 实测不能依赖 `cwd` 解析相对 IOC 路径，否则 `-q` 也可能弹出找不到 IOC 的错误窗口。macOS 默认查找 `/Applications/STMicroelectronics/STM32CubeMX.app/Contents/MacOS/STM32CubeMX`，也支持 `--cubemx <可执行文件>`、环境变量 `STM32CUBEMX` 或 PATH 中的 `STM32CubeMX`。`--dry-run` 只打印计划，`--timeout` 设置生成超时（默认300秒）。
- 每次实际执行先将 Board 内容备份到根 `build/cubemx/<时间戳-唯一后缀>/Board-before`（排除 build、.idea、__pycache__），同目录保存实际 `.mx`、生成日志、时序检查日志和变化文件清单。脚本不自动回滚、不构建、不烧录、不操作调试器；生成失败仍保留现场与快照供比较。调用脚本由 `generate.py` 根据当前检出目录临时生成。
- 不能仅凭进程退出码宣称成功：核对 `project generate` 的 `OK` 和生成内容。脚本会检查退出码/成功标记，并运行 `Tools/Debug/check_i2c_timing.py`，要求 IOC、生成的 I2C4 TIMING 和 Support 常量一致。不要为消除检查失败而盲目同步常量，应先核对时钟、滤波、时序及此前板测依据。
- 生成后与本次快照比较，检查 USER CODE、SDK 自维护 CMake/Support/Linker、HAL/CMSIS、工具链和中断所有权，保留原有未提交修改；再进行对应固件构建，涉及生成清单或构建接入时重新构建 Minimal 和实际父项目。生成、编译与板端验证分别报告，不覆盖当前调试会话所用 ELF。
- CubeMX 版本以实际运行日志为准；每次生成都检查差异和 USER CODE，不假定只变更某个时序字段。
- 用户明确要求仅修改 IOC、不要 Generate 时遵从该范围；已授权的生成工作无需重复询问。Generate 不隐含授权重新烧录或接管用户暂停现场。

### 当前 Kernel 实现边界

- FreeRTOS 10.6.2、CMSIS-RTOS2、heap_5 与原工程 CM4F 浮点 port 固定快照位于 `ThirdParty/FreeRTOS/`，来源、许可证和 SHA256 清单同目录维护；当前未修改第三方源码。
- `Config/FreeRTOSConfig.h` 由 SDK 管理，不由 CubeMX 生成。使用 Picolibc TLS，关闭 Newlib reentrancy；trace 统一经 私有 `Source/Diagnostics/Runtime/trace_internal.h` 分发至 Fault 元数据和 HSKP。不得恢复指向旧工程的 hooks。
- FreeRTOS/CMSIS 拥有 SVC、PendSV、SysTick，HAL 继续以 TIM7 计时。FromISR API 的中断优先级数值必须为 5～15。
- `Board/Support/System/kernel_board.c` 定义按地址递增的 DTCM 64 KiB + D1 128 KiB heap_5 区域；`osKernelInitialize()` 是 heap 注册的唯一入口。最终内存布局由 `Board/Linker/SirinEmbedOS.ld` 管理，普通 data/TLS/BSS 在 D1，DTCM 留给热数据、heap 和 MSP。NOLOAD heap payload 不保证清零，两区均不是 DMA 分配器。
- DMA 位于非缓存 D2，BDMA 位于 D3；CAN Router 专用 BSS 与 Watch 位于 ITCM，按 64 位显式清零，ITCM 代码按 64 位复制。三者共享 64 KiB；专用段禁止构造期访问。Fault retained 留在 D2 并验证。Picolibc TLS 占用线程栈；独立 libc arena 和静态递归 RTOS 锁已接入，禁止在 ISR 分配或调用阻塞 stdio，不宣称任意共享 FILE 都线程安全。
- Fault 向量由 SDK glue 持有，异常捕获不依赖日志或动态分配。紧急输出默认在不可恢复 Panic/Fault 阶段接管 USART1 TX（PB14，115200/8N1）有界轮询输出；正常启动不占用，可在配置阶段显式注册回调覆盖。正常日志只经自己的 Serial/USB sink。细节见 `Source/Core/README.md`。
- 构建接入说明与配置合同见 `README.md`；构建与验收边界见 `Docs/Development.md`。构建成功不代表板端调度与外设验证。

## 核心定位：固定硬件上的通用开发平台

**目标不是硬件通用或跨 MCU 移植，而是针对当前这款主控硬件，提供可复用的应用开发平台。** 当前基线是原工程对应的 STM32H723 / OmniCtrl 2 系列主控；具体型号、板卡修订、引脚和连接以当前 IOC、原工程实现及已确认的硬件资料为准。

- 通用性体现在：不同应用能复用同一块板上的资源、通信、算法和系统服务。
- 硬件已经定死的能力可以直接成为平台默认组成，不必为了假想的其他板卡增加抽象层、运行时探测或大量开关。
- CAN、板载 IMU 等固定硬件能力默认包含。实现时确认实际总线数量、设备型号、引脚和资源绑定，不凭模块名称猜测。
- 默认包含表示平台提供能力并安排必要的初始化；不表示上电自动控制外部执行器。电机使能、控制目标和运动行为仍由应用明确决定。
- UART 等既可直接使用、又可承载遥控、裁判系统、主机通信或其他协议的资源，需要保留用途选择与配置空间。
- 不要求所有外设都采用相同程度的可配置性；根据本板真实的使用方式决定接口与配置。
- 不为追求形式上的通用性引入插件框架、复杂设备树、多个 RTOS backend 或没有实际需求的硬件适配层。

## CubeMX 与 SirinEmbedOS 的职责

目标是让 CubeMX 只管理硬件配置与 HAL 生成内容：时钟、GPIO、引脚复用、外设参数、DMA、NVIC、HAL/MSP 初始化和启动基础代码。

SirinEmbedOS 自行管理：

- FreeRTOS 源码版本、配置、heap、hooks、同步资源与调度启动。
- FatFs、USB Device/Composite、LwIP 等 middleware 的源码版本、配置、编译与生命周期。
- CAN、IMU 等默认硬件服务，以及日志、参数、主机通信、诊断等公共能力。
- 系统线程与应用线程的启动装配、资源预算和就绪状态。

项目侧保留具体应用配置、可变端口用途和业务任务。CubeMX 生成文件只在 `USER CODE` 区域承接稳定入口，不在生成区塞入系统服务或业务逻辑。

当前基线由 CubeMX 生成 HAL，FreeRTOS 与双 CDC USB 的源码、配置和生命周期由 SDK 装配。以后重新 generate 仍需复查生成清单和中断所有权。旧工程“在 CubeMX 保持 middleware 开启、通过 linker wrap 接管”的规则是旧方案，不恢复这种管理方式。

HAL tick 与 FreeRTOS tick、中断向量与 handler、外设初始化与 callback 都必须有明确且唯一的所有者。关闭 CubeMX middleware 后，复查生成结果，不假设相关中断和 HAL 配置自动保持正确。

### 固定硬件能力

- CAN、板载 IMU 的板级绑定允许直接固化在平台实现中，对外提供稳定、易用的 API。
- 固定硬件不意味着让应用直接操作 HAL handle；总线访问、异步完成、并发与故障处理由平台统一管理。
- 原工程已有 router、驱动和算法优先作为参考，核对硬件约束和有效行为后，按本工程边界决定复用、重构或重新实现；不为保持旧结构或旧命名而牺牲本工程接口与职责的一致性。
- 合并模块或仓库，不等于合并线程。调整线程数量、优先级、周期和队列前，先说明调度与资源影响。

### 可变用途资源，特别是 UART

- 允许应用将可用 UART 配置为原始收发，或绑定平台支持的协议功能。
- 物理端口、引脚和 DMA 受本板实际布线约束；灵活配置不表示任意重映射硬件。
- 同一端口的所有权必须清楚。直接访问与协议服务不能绕过资源管理同时占用；确需共享时必须有明确的仲裁或复用协议。
- 所有架构配置均在启动阶段完成选择、检查和冻结，包括 CAN、UART、IMU 和系统服务；平台不支持运行时热切换、rebind 或配置重载。错误恢复只使用已经冻结的配置。
- 配置应覆盖真实需求，例如端口用途、波特率、收发模式和缓冲容量，而非为每个内部实现细节暴露开关。

### 接口与资源合同

- API 明确单位、超时、返回状态、线程/ISR 调用限制和数据所有权。
- 异步 I/O 明确 buffer 生命周期及完成通知；不提前复用尚在传输中的内存。
- 优先沿用已迁入的 `SRNOS_*` / `SRNOS::` 基础操作，避免重复实现字节复制、边界检查、Endian、Assert/Panic 等能力。不新增旧名兼容别名。
- SirinLink 可写入的仅限显式 `SRNOS::Parameters::Persistent<T>` 或对应 C 描述声明且有默认值、稳定 ID 和范围的业务数值，例如 PID 增益；遥测使用只读且不可持久化的 `SRNOS::Telemetry::Value<T>`。系统底层选择、UART/CAN/IMU 配置、线程预算和服务开关不得放入可远程修改的参数表。注册和访问权限初始化后冻结；业务代码通过一致快照在自己的周期边界应用数值，不让通信线程直接修改控制器内部状态。
- 新接口命名、旧 API 兼容范围和组件目录应结合迁入的实际实现确定，不凭本文假定它们已经存在。

## 重构与验证规则

- 必要的功能目录必须同时保留 `README.md` 和 `CHANGELOG.md`，例如承担独立能力的 IO、设备、服务或诊断模块目录。以能独立说明职责、接口和验证范围的模块为单位维护，不要求每一层目录或每个私有实现子目录重复建立文档；缺失时在该模块下一次重大修改中补齐。
- 每次重大修改功能、公共接口、配置合同、架构、调度或资源所有权时，必须在同一次变更中更新受影响模块的 `README.md` 和 `CHANGELOG.md`。`README.md` 只描述修改后的当前能力、用法、示例、边界与限制；每次修改的日期、原因、过程、验证结果和待验证事项只记录在对应 `CHANGELOG.md`。行为未变化的修订无需制造 README 更新记录；不得把历史日志追加到 README、QuickStart、专题文档或本文件。
- `QuickStart.md` 保留一条能实际编译的最短接入路径；专题文档与模块 README 必须给出准确 API、命令或阅读示例，区分完整程序与需集成的片段。配置、同步、错误处理和所有权按当前源码核对。
- 历史变更记录归入所在目录 `CHANGELOG.md`；公开树不携带测试套件、专用测试固件、实机记录和转储。迁移文字时保留测量结论与未验边界，修复相对链接，不把当时板上状态写成当前状态。
- 各级 `CHANGELOG.md` 保持浅层结构：优先使用日期或 `Unreleased` 标题配合平铺条目，必要时增加一层分类；避免多层标题和嵌套列表，不按目录树逐层展开日志。
- 根目录 `CHANGELOG.md` 记录有意义的工程、接口、配置与架构变更，按日期维护。未发布内容放在 `Unreleased`；没有实际发布时不编造版本号。同步记录验证范围和重要限制，计划中的能力不能记为已实现。
- 所有交流使用中文。用户要求分析、查看或先不改代码时保持只读。
- 修改前说明涉及哪些文件、目的，以及是否影响生成边界、公共 API、调度或内存布局。
- 保留用户已有修改，不回滚无关内容，不自动提交、推送或烧录。
- 每次迁移一个边界清楚的能力，先追踪原工程调用链、配置来源和依赖，再决定新位置。
- 旧工程的 Framework 多为 submodule；新工程不必沿用碎片化仓库结构。迁入第三方或共享代码时保留来源、版本与许可信息。
- 参考或复用原实现时，优先保持已确认适用的硬件时序、协议语义和第三方依赖版本；这不要求保留旧架构、接口或命名。不把结构迁移与第三方升级混在一次变更中；有意改变行为时说明原因并补充相应验证。
- DMA/cache/MPU、heap、链接段与 Debug Watch 相关工作，先查看参考工程的 `lib_mpu_dcache`、`lib_debug_watch` 及相关链接配置；迁入后以本工程维护的实现为准。
- 不将 DMA buffer 放入 DMA 不可达内存；保留必要的对齐、非缓存区和 NOLOAD 显式初始化规则。
- 构建系统由项目维护，避免依赖 CubeMX 扫描目录自动决定 middleware 和系统服务是否参与编译。
- 验证层次明确区分：IOC 保存/重开、CubeMX 生成、静态检查、主机测试、固件编译、板端运行。编译成功不等于外设或系统服务已经可用。
- 固件具备构建条件后，迁移应完成适当的编译检查；内存布局变化需检查 map/size；运行时或硬件行为变化仍需对应实机验证。

### 接入与会话所有权

- CLion MCP 适合与用户共用 IDE 的断点、调用栈和变量视图；Agent 直接使用 ARM GDB + J-Link GDB Server 适合命令化取证和重复采样。两条路径均可用，但同一探针/目标同一时间只由一条路径控制，不同时启动两个 GDB Server 或抢占已有连接。
- 用户仅问“能否连接”时，只检查 MCP 握手、工具枚举、进程和监听端口；不启动板端调试。MCP 可达、调试工具已加载、调试会话存在和板卡已连接是不同事实，分别报告。
- 操作前核对现有 CLion/GDB/J-Link 会话、目标工程、探针和暂停状态。用户保留的暂停现场不得主动继续、停止、断开、复位或覆盖；只有用户授权继续该调试工作时才操作。已授权的调试范围内可连续取证，不逐条重复询问。
- 查看卡点默认先保留现场，不执行 `load`、擦除、`monitor reset` 或固件下载。启动 IDE 调试配置前，检查其构建、下载、复位和初始化动作，不能把“开始调试”等同于无副作用的 attach。

### CLion MCP

- 从当前工具列表确认 `mcp__clion__*` / `xdebug_*` 或 `execute_tool` 是否实际可调用；只有配置条目不代表当前会话已加载。优先使用原生 MCP 工具，必要时可用本地 HTTP 做握手和工具枚举诊断。
- 在 CLion 的 Settings → Tools → MCP Server 开启服务，并通过 Codex Auto-Configure 或 Copy HTTP Stream Config 获取实际地址；核对 `~/.codex/config.toml` 中的配置，更新后重启/重新加载客户端。不要把端口写成永久固定事实。
- CLion MCP 地址和端口从当前 IDE 设置与握手核对，不沿用历史端口。
- 调试工具依赖 Debugger MCP Toolset。若使用 router-only 模式，确认 `execute_tool` 可用，并按需从 Settings → Tools → AI Assistant → Skills 将 `clion-debugger` 安装到 Codex (Global)；不要只搜索 `xdebug_*` 就断定没有调试能力。
- 先读调试状态并选择正确会话，再读寄存器、栈和变量。表达式求值不自动等于只读：避免调用目标函数、带赋值的表达式或会产生副作用的外设寄存器读取。

### Agent 直接使用 J-Link / GDB

- 使用明确的可执行文件绝对路径，先核对已安装版本与符号链接，不据历史 J-Link/GDB 路径推断当前安装。

### 结束与验证边界

- 结束时明确 MCU 是运行、暂停还是已断开，GDB/J-Link 是否仍占用探针，以及保留了哪些断点和日志。`detach`、关闭服务器或退出调试器可能改变目标运行状态，不作为无副作用的自动清理步骤；用户要求保留现场时保持暂停并说明。
- 暂停/单步会扰动 FreeRTOS、总线超时和设备时序，全任务栈扫描也会挂起调度。断点取证不代表实时性验收；故障修复须区分主机测试、编译链接、修复固件下载和修复后板端复测，不以旧固件现场证明新固件正常。

## 固定平台合同

- CAN1–3 各有 RX/TX 线程，CAN4 使用单个服务线程。CAN1–3、BMI/LSM/INS 为优先级 48，CAN4 与 UART 服务为 47，SPI4 为 49、I2C4 为 46；修改优先级、IRQ、队列或栈前说明资源与时序影响。
- 四路 CAN 在启动时各选 Classic 或 FD；现有电机 adapter 要求 Classic 1 Mbit/s。原生 RX 每轮最多 32 帧，每批搬运最多 4 帧；FD 负载不能直接套用 Classic 的吞吐结论。时间使用 SRNOS_TimeMs。
- 板内安装坐标由 Board 支撑维护，应用通过 INS 的 board_to_robot_wxyz 配置机器人安装旋转。消费者检查 VALID、年龄和 epoch，RUNNING 不代表姿态有效。
- INS worker 负责统一温控；ins.enabled=false 不会取消传感器或 INS worker。IMU 观察回调仅做有界复制/通知，不等待、打印或编码整包 USB。
- 应用明确配置加热用途；指示器复用 SDK 服务，不新增专用 LED 线程。启动提示与运行心跳的实际声光须现场观察。
- 身份、IMU 校准和业务参数由 Storage 单份 CRC/提交记录管理。保存失败不保证介质未改变；校准在下次启动应用，业务参数在自身周期边界读取一致快照。

## 文档维护示例

例如调整 UDP 接收策略时：在 `Source/Services/Network/README.md` 更新配置与消费示例，在该目录 `CHANGELOG.md` 写原因、兼容性和验证边界；公共入口变化再更新 `Docs/SDKMigration.md` 的迁移步骤。不要在这些用法文档追加日期和测试战报。

```sh
# 从 SDK 根目录执行；仅离线检查，不连接 PCB。
cmake --preset Debug -DSIRINEMBEDOS_EXAMPLE=Minimal -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build --preset Debug
```

根目录与模块指南互相链接，所有示例使用真实公共接口。历史来源见 [迁移索引](Docs/Migration/README.md)，功能变更见对应 CHANGELOG。
