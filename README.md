# SirinEmbedOS

面向固定 STM32H723 / OmniCtrl 主控的 SDK。应用通过一个 Git 子模块引入整个 SDK，自行管理控制算法、业务线程与配置；SDK 内不保留原 Framework 的嵌套子仓库。CubeMX 只管理硬件与 HAL，调度和系统服务由 SDK 管理。目录职责见 [AGENTS.md](AGENTS.md)。

目前已将 prototype 的基础操作、Fault、内存、诊断、CAN/SPI/I2C、固定设备、串口协议、日志、参数、HostLink 和双 CDC USB 能力纳入源码与显式构建清单。自维护公共 API 统一为 `SRNOS_*` / `SRNOS::`，旧名称、旧头路径和兼容别名已移除。父应用只包含 `Include/SirinEmbedOS/` 公共头；FreeRTOS、CMSIS、HAL 和 SDK 私有头属于实现依赖。升级方式见 [源码迁移指南](Docs/SDKMigration.md)，模块去向见 [迁移清单](Docs/Migration/README.md)。FatFs、LwIP 尚未接入。

已完成主机测试和固件编译链接。用户首次板端调试确认可进入 RUNNING 并采到 IMU 数据，同时发现启动事件屏障误判导致复位循环；源码已修复，随后直接连接及一次软件复位复测未再现旧断言，IMU 数据持续更新；完整外设和压力验收仍待完成。当前验证结果和待测项见 [验证记录](Docs/Validation.md)。

2026-09-28 全面命名与事件等待改造后已重新烧录：两组统计开关对照、托管任务生命周期、ADC 软件故障注入及 CDC 背压/DTR 重连通过。统计开销约 0.99 个百分点，接近目标边界；当时实际 CAN 负载、串口、仪表精度和长期压力仍未验收。当前证据见 [SRNOS 板端记录](Docs/Validation/SRNOSBoard-20260928/README.md)，后续功能缺口与优化顺序见 [开发清单](Docs/Development.md#当前底层开发与优化优先级)。

后续已完成 CAN1↔CAN3 外接负载短测并修复通知 weak 链接错误：合计 8000 帧/s 全部交付，但整板 CPU 95.21%，业务余量不足；CAN2/CAN4 与故障恢复仍待测。ADC 24 V 仪表单点读到均值 24.641 V，保持标称倍率待进一步核对。详见 [最新实测](Docs/Validation/CAN13-ADC-20260928/README.md)。

## 文档入口

[完整导航](Docs/README.md)按父项目开发、持久化、协议、设备和验证组织。常用入口：

- [EEPROM 存储设计](Docs/Storage.md)：单份分区/CRC/提交页、身份/校准/参数正文、默认值与保存代次。
- [COBS/HostLink 协议](Docs/HostLink.md)：帧字段、消息号总表、参数读写、SOURCE V1/V2 和静态校准命令。
- [托管线程与同步](Source/Kernel/README.md)、[参数示例](Examples/Parameters/README.md)、[开发机采集工具](Tools/HostLink/README.md)。

2026-09-29 已完成持久化板测，并按实测噪声提高静态校准阈值：连续两次双IMU校准保存成功，软件复位后revision2加载一致、INS未校准标记清除。当前保留新固件及校准记录，见 [阈值复测](Docs/Validation/StaticThreshold-20260929/README.md)。参数事务和未处理的遥测吞吐结果见 [初轮板测](Docs/Validation/PersistenceBoard-20260929/README.md)；真实断电、校准绝对精度仍待验收。

## 构建与 CLion

使用 **ST Arm Clang 21.1.1 / Picolibc**（本机 STM32CubeCLT 1.22.0）。GCC、Newlib、其他编译器尚未纳入这套构建验证。

```sh
export PATH="/opt/ST/STM32CubeCLT_1.22.0/st-arm-clang/bin:$PATH"
cmake --preset Debug
cmake --build --preset Debug -j 8
```

产物为 `build/Debug/SirinEmbedOS.elf` 和 `.map`，Release 使用同名 preset。CLion 打开仓库根目录，工具链环境提供上述 bin 路径；`.idea/` 和构建输出留在根目录。更换工具链后刷新 CMake 缓存，例如 `cmake --fresh --preset Debug`。

`Board/` 独立构建也转入同一 SDK，使用 `Board/CMakePresets.json`，产物为 `Board/build/Debug/sdk/SirinEmbedOS.elf`。CubeMX 生成清单、启动汇编和原始链接脚本留在生成位置；最终固件使用 SDK 维护的 `Board/Linker/SirinEmbedOS.ld`。

## 父项目接入

在 `project()` 前选择 SDK 工具链，再引入 SDK 和应用源码：

```cmake
cmake_minimum_required(VERSION 3.22)
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/SirinEmbedOS/CMake/starm-clang.cmake")
project(MyRobot LANGUAGES C CXX ASM)

# 可选；同一构建内全部 SDK 对象使用同一份配置。
set(SIRINEMBEDOS_CONFIG_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/Config/system_config.h"
    CACHE FILEPATH "Application configuration")
add_subdirectory(SirinEmbedOS)
sirinos_add_firmware(MyRobot App/app_main.cpp)
```

也可自行创建 executable 并链接 `SirinEmbedOS::Platform`；helper 另外设置 `.elf` 后缀和按目标命名的 map。Platform 提供 Board、HAL、Kernel、组件对象、启动与链接配置，但公共使用要求只暴露 SDK Include 和明确配置；不会把内部头搜索路径传播给应用。不要在父项目手工补入 `Source/`、`Board/`、`ThirdParty/` 或 RTOS include 路径。SDK 不重置父项目名或工具链，作为子目录时默认不创建示例固件。

应用实现以下公开入口，不再定义另一个 `main()`：

```cpp
#include <SirinEmbedOS/Kernel/Startup.h>
#include <SirinEmbedOS/Kernel/Thread.h>

void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config)
{
    // 仅启动声明；此时调度器尚未初始化。
    config->application_stack_bytes = 8192;
    config->load_parameters = false;
}

static SRNOS_StepResult step(void *, const SRNOS_ThreadWake *)
{
    // 父项目提供一轮业务，SDK 负责等待、循环和统计。
    return SRNOS_STEP_CONTINUE;
}
static SRNOS_Thread worker;
static SRNOS_Result worker_create_result = SRNOS_BUSY;
void SRNOS_ApplicationMain()
{
    SRNOS_ThreadConfig task{};
    task.name="Control"; task.step=step; task.stack_bytes=4096;
    task.priority=24; task.mode=SRNOS_TASK_PERIODIC; task.period_ms=1;
    worker_create_result = SRNOS_ThreadCreate(&task, &worker);
    if (worker_create_result != SRNOS_OK) return; // 由应用报告或安排重试。
    // 成功创建后可返回；worker 继续运行，init 结果另外用 GetInfo 查询。
}
```

`Examples/Minimal` 验证公开启动入口；[BringUp](Examples/BringUp/README.md) 提供首次上板的启动/堆/任务栈/IMU 观测；[SensorBringUp](Examples/SensorBringUp/README.md) 验证 2613 原始传感器与共享总线，关闭融合和业务参数加载/主机存储命令（启动仍读取身份与 IMU 校准），默认使用统一60°C温控，并提供 [CDC1 双 IMU 遥测 CLI](Docs/HostLink.md)；[Parameters](Examples/Parameters/README.md) 展示显式业务 PID 参数和一致快照；[PrototypeBindings](Examples/PrototypeBindings/README.md) 展示 prototype 的 DR16、VT03、裁判与日志端口映射。选择一个应用入口，可按说明组合串口绑定文件。迁移收尾顺序见 [后续开发清单](Docs/Development.md)。

## 托管任务与父项目内核接口

父项目业务线程统一使用 `SirinEmbedOS/Kernel/Thread.h`，同步对象使用 `Sync.h`。一般业务代码不直接调用 `xTaskCreate`、`vTaskDelete`、`vTaskDelay`、`osThreadNew` 等 FreeRTOS/CMSIS API，也不提供统计 hook。SDK 负责外层循环、等待、生命周期和统计；App 提供 `init/step/cleanup`。旧 `SRNOS_ThreadConfig.entry` 已删除，无兼容层。

| 建立方式 | 适用场景与管理方式 |
|---|---|
| 在 `SRNOS_ApplicationMain()` 或应用任务中调用 `SRNOS_ThreadCreate()` | 需要保存句柄、发送通知、查询、停止或回收时优先采用。先准备好参数/同步对象，成功后保存输出句柄；运行期失败由应用处理。上面的接入示例使用此方式。 |
| 在 `SRNOS_ApplicationConfigure()` 中填写 `config->threads[]` 和 `thread_count` | 随系统启动的固定任务。此处只声明，SDK 随后创建并在应用就绪屏障后放行；不要对同一任务再调用 Create。配置列表不向父项目返回句柄，见 [启动声明与句柄](Source/Kernel/README.md#启动配置中的固定任务)。 |

`SRNOS_ApplicationMain()` 是调度后的启动入口，创建任务后可以返回，子任务继续运行。不要把永久业务循环放在这个入口；其自身属于 SDK 启动线程，不适用父项目线程通知/删除接口。启动列表中的任务可能先于该入口运行，不能依赖该入口先完成共享资源初始化。

`step` 必填，只完成一轮工作；`init`、`cleanup` 可选。周期模式 `SRNOS_TASK_PERIODIC` 在初始化成功后立即执行首轮，随后按 `period_ms` 固定相位运行，跳过错过的周期；`step` 中不再写周期 Delay。事件模式 `SRNOS_TASK_EVENT` 配置通知掩码及等待超时，检查 `wake->reason/flags`；单次模式 `SRNOS_TASK_ONCE` 只执行一轮。有限 I/O 阻塞可以保留在回调中。

| 管理操作 | 父项目应遵守的规则 |
|---|---|
| Create / GetInfo | Create 成功只表示对象创建成功，回调可能在 Create 返回前执行；用 GetInfo 查询 `init_result` 和最终状态。 |
| RequestStop / Join | 请求协作停止；Join 等待 cleanup 完成，不回收对象。正在执行的回调必须自行返回，设备/同步等待应有有限超时。 |
| Delete | 自带停止、等待和回收。只有返回 `SRNOS_OK` 后，才能清空句柄、释放参数及其专属资源；TIMEOUT/BUSY 时保留并稍后重试。自然退出或初始化失败也必须显式 Delete。 |
| GetCurrent / ShouldStop | 在回调内取得自身句柄、检查停止请求。自身结束返回 `SRNOS_STEP_STOP/FAILED`，不能同步 Join/Delete 自己。 |
| Delay / DelayUntil | `SRNOS_Delay(0)` 让出执行权；`SRNOS_DelayUntil()` 接受 `SRNOS_TimeMs()` 域中的绝对毫秒截止点。已过期返回 `SRNOS_DEADLINE_PASSED`；不要把 `TimeUs()` 换算后当作此截止点。 |

创建、删除和阻塞限任务上下文；ISR 只使用明确提供的 `FromISR` 接口。线程名须唯一、1～15 字节，禁止 `SRNOS_` 前缀；父项目优先级 1～39，栈按字节配置、8 字节对齐且包括 TLS。描述及名称在实际创建时复制，`argument` 指向的内存必须保留至 Delete 成功，不能传入启动回调即将失效的局部对象。

默认父项目对象池 32 KiB、线程上限 8、互斥锁/信号量/队列/事件标志各 8；启动声明与运行期创建共用限额，栈和对象共用池。8 个 4 KiB 栈之外还需对象开销，默认池不保证能同时创建这些任务。用 `SRNOS_KernelGetResources()` 查看用量和失败计数；通过父项目配置头覆盖允许的容量，硬件/服务配置仍在启动冻结。

完整 [Kernel 使用指南](Source/Kernel/README.md) 包含启动声明、运行期创建、事件/同步、协作删除、延时及旧 API 迁移；[Minimal](Examples/Minimal/README.md) 对应可构建的最小创建示例。`cycle_statistics_enabled` 控制内部循环统计，`system_monitor_enabled` 控制后台采样，父项目无需插入 hook。基础流 0x0001、ADC 0x0002、循环流 0x0003 的口径及板测范围见 [SystemMonitor](Source/Services/SystemMonitor/README.md) 和 [板端记录](Docs/Validation/ManagedTasksBoard-20260928/README.md)。

## 配置与生命周期

启动顺序为专用段/Fault 早期初始化 → 生成的 HAL 初始化 → 应用配置与服务声明 → 全量校验、注册表及系统配置冻结 → `osKernelInitialize()` → 创建系统与应用线程 → 唯一一次 `osKernelStart()`。调度后在共同 2 s 预算内依次尝试读取身份、IMU 校准及业务参数（默认启用；业务参数可单独关闭），再启动 USB，最后放行应用入口和配置中声明的应用线程。应用入口允许返回，返回时仅退出该线程。

CAN、UART、IMU、服务选择、资源预算和绑定均在初始化时冻结。配置 hook 不做阻塞 I/O，不创建线程；初始应用任务可通过 `SRNOS_SystemConfig::threads` 声明，就绪后也允许通过 `SRNOS_ThreadCreate` 创建有界运行期对象。`bind_channels` 在内置协议完成声明后用于原始串口、H12H 或 RS485 设备的所有权登记。没有运行时重绑定、端口切换或系统配置重载。

默认集成三路原生 CAN、外置 CAN、板载 IMU、EEPROM、板级指示器和诊断；USB 默认提供 CDC0 日志与 CDC1 HostLink。普通 USART1/USART6/UART8/UART9 默认未绑定，不会自动占用遥控或日志用途。UART7 是板载固定 RS485 通路，外接设备仍由应用显式声明。H12H 默认未绑定。默认包含总线能力不代表上电自动使能电机或执行运动。

Serial 统一拥有 HAL 收发、DMA 缓冲与回调。端口只能被一个 owner 占用；协议、原始接口和日志不可绕过占用管理并发使用同一端口。`GetChannel()` 仅在初始化时返回未占用通道，成功声明后由所有者保留该指针；配置了但未声明 owner 的端口会阻止启动。具体缓冲、超时、线程/ISR 限制以公开接口为准。

父项目声明两类变量：`SRNOS::Parameters::Persistent<T>` 用于有默认值/范围、稳定 ID 的业务参数，可由 HostComm READ/WRITE/SAVE；`SRNOS::Telemetry::Value<T>` 仅发布带时间戳的只读最新值，不进入 EEPROM。系统路由、CAN 配置、服务开关和预算不属于参数表。默认启动加载与业务 SAVE/LOAD 已启用，应用必须给持久参数设置稳定的 `parameter_application_id`（兼容版本默认 1）；无参数可保留 ID 0。应用在自身周期边界使用一致快照，不让通信线程直接改控制器内部成员。

```cpp
#include "SirinEmbedOS/Services/Parameters/Parameters.hpp"
#include "SirinEmbedOS/Services/Telemetry.hpp"
#include "SirinEmbedOS/Kernel/System.h"

SRNOS::Parameters::Persistent<float> kp{0x1001, "motor.speed.kp", 1.0f, 0.0f, 50.0f};
SRNOS::Telemetry::Value<int32_t> encoder{"motor.encoder", "count", 1000};
extern "C" void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *c) {
    c->parameter_application_id = 0x524F424F; // 本应用稳定标识，升级时保持
}
// 任务每轮：encoder.Publish(encoder_count, SRNOS_TimeUs());
// 低优先级管理任务：SRNOS_ParametersSave();  // WRITE/Get/Set本身不写EEPROM
```

FT24C128A 采用单份身份/逐器件校准/参数记录，CRC32 校验与最终提交页检测不完整写入。没有备份：保存中掉电可能失去该记录，启动参数回默认、IMU 忽略校准并明确降级；运行期 LOAD 失败则保留当前 RAM。序列号为 20 位数字字符，仅专用本地工厂权限可写。校准在 SENSOR 轴应用，新保存值下次启动生效。上位机可通过 CONFIG `0x09`（正文 `01 03`）触发双 IMU 任意姿态静止陀螺零偏校准：约 5 s 采集，异常返回错误，EEPROM 校验保存后才成功；`0x0A` 查询进度。该流程不估计加速度零偏/比例，具体合同见 [静态校准](Source/Services/INS/README.md#手动静态校准)。详见 [Storage](Source/Services/Storage/README.md)、[Parameters](Source/Services/Parameters/README.md)、[Telemetry](Source/Services/Telemetry/README.md)、[IMU 校准](Source/Services/INS/README.md#校准持久化)。SOURCE V2 使用 TELEMETRY 0x10–0x14，最高 1 kHz/源、总 8000 标量/s、最多 32 源；最新值允许拥塞丢旧，不是完整采样记录。

2613 的 FT24C128A 已提供独立的 [阻塞/非阻塞器件 API](Source/Devices/EEPROM/README.md)，完成 16 KiB 整片交叉读写、边界和备份恢复板测；使用 `SIRINEMBEDOS_EXAMPLE=EEPROMBringUp` 构建 [专用验证应用](Examples/EEPROMBringUp/README.md)。参数/INS 上层存储尚未迁移。[I2C4 分发线程](Source/IO/I2C/README.md) 为 LIS/SPA 保留周期槽，EEPROM 读写/校验按 32 B 分段。当前采用 400 kHz Fast mode（数字采样约 370 kHz）：整片写入含校验约 5.12 秒、读取约 1.03 秒，压力下 LIS 约 97–99 Hz、SPA 约 16 Hz，持续 READY，无总线错误或超时。21 项板测通过，按用户要求保留测试数据与验证固件；尚未测量模拟边沿、掉电或断线，见 [提速记录](Docs/Validation/I2C400-20260928/README.md)。

提速配置已通过 CubeMX CLI `-q` / `project generate` 同步到生成代码，IOC、生成的 I2C4 初始化和 Support 时序均为 `0x10C7182D`。日常运行 `python3 Board/generate.py` 自动定位 IOC、备份 Board、调用 CLI 并核对时序；使用方法与生成边界见 [Board 说明](Board/README.md)，生成后的构建与此前板测分开记录。

`SRNOS_SystemGetStatus()` 反映启动阶段、原生 CAN 初始化、USB 启动及参数加载结果；`RUNNING` 不是全部设备健康证明。USB 启动成功不等于主机已枚举或打开 CDC，外置 CAN 和 IMU 在各自 worker 中初始化，需要检查对应状态与诊断；应用必须自行决定未就绪时的业务行为。

## 内存与内核合同

父项目配置头可覆盖 `SRNOS_APP_STACK_BYTES`、`SRNOS_APP_PRIORITY`、`SRNOS_HEAP_DTCM_BYTES`、`SRNOS_HEAP_D1_BYTES`，默认分别为 4096 字节、CMSIS Normal（24）、64 KiB、128 KiB。配置 hook 可进一步声明初始应用线程预算。`SRNOS_SystemHeapBudget()` 提供保守启动预算校验，应用运行期额外分配仍需自行预留容量。不同 SDK 配置使用不同构建目录。

- FreeRTOS 10.6.2 / CMSIS-RTOS2 / heap_5 固定快照随 SDK 管理，Kernel tick 为 1 kHz；HAL tick 使用 TIM7。SVC/PendSV/SysTick 由 FreeRTOS/CMSIS 持有；调用 FromISR API 的 IRQ 数值优先级必须在 5～15。
- 普通 data/TLS/BSS 放在 D1；CAN Router 的 CPU 专用上下文和 `.debug_watch` 放在 ITCM，DMA 缓冲不进入 TCM。DTCM 保留热数据、heap_5 首区和 MSP。默认 MSP 与独立 libc arena 各 8 KiB，可通过同名 CMake 缓存项 `SRNOS_MSP_BYTES` / `SRNOS_LIBC_BYTES` 调整。libc arena 与 RTOS heap 分离。
- DMA 专用段在非缓存 D2，BDMA 缓冲在 D3。ITCM BSS/Watch 在配置回调前以 64 位写入清零，代码按 64 位复制；禁止全局构造函数访问这些尚未初始化的专用段。NOLOAD DMA/BSS/Watch 段由早期启动显式初始化；Fault retained 区不清零。普通 heap_5 分配不是 DMA 分配器，payload 不保证清零。
- 当前 Minimal Release：D1 空余 73344 B，D2 空余 25920 B，ITCM 空余 2528 B。迁移释放 D1 59904 B、D2 2848 B，未扩大 RTOS heap，也未迁入 Monitor/遥测缓存；完整预算及板测边界见 [ITCM 迁移记录](Docs/Validation/ITCM-20260929/README.md)。
- Picolibc TLS 占用线程栈，栈预算须包含 TLS。libc 锁由静态递归 RTOS mutex 接入，ISR 不可调用分配器或阻塞 stdio；该适配不代表任意共享 `FILE` 操作都线程安全。
- Fault 默认记录 RAM 并按策略 halt/reset，不抢占或复位 USART1。紧急输出只能在配置阶段显式预留；正常日志走自身队列和 Serial/USB sink。

具体内存段、异常和锁规则见 [基础层说明](Source/Core/README.md)。各第三方目录保留来源、许可证和哈希；AL94 1.0.3 仅纳入 Core、COMPOSITE、CDC_ACM，双 CDC class 使用静态存储。

## 验证

```sh
python3 Tests/run_host_tests.py
python3 Tests/Build/check_structure.py --compile-commands build/Debug/compile_commands.json
python3 Tests/Build/check_sdk.py
```

构建回归检查十种父项目固件接入、C11/C++17 公共头独立及联合编译、依赖闭包、真实应用编译命令、命名、配置覆盖、第三方哈希、链接符号和内存布局；能力主机测试位于 `Tests/`。命令、结果与硬件待测项集中在 [验证记录](Docs/Validation.md)。编译、静态检查和主机仿真不替代 USB 枚举、UART DMA/IDLE/错误恢复、CAN 收发、IMU 采样、Cache、Fault 和实际调度验证。

## 正式 INS

默认启用双六轴 Mahony INS 与60°C恒温控制，固定板内旋转、启动冻结的业务安装四元数、多消费者快照和降级状态见 [INS合同](Docs/INS.md)。`SIRINEMBEDOS_EXAMPLE=INSBringUp` 提供无周期栈扫描的观测入口；原始对比用 SensorBringUp。旧单BMI融合/校准/温控路径已删除，默认在启动时加载单份校准及参数记录。

## 板级维护入口

固定板级接入位于 [Board/Support](Board/Support/README.md)，按 System/IO/Devices/USB 分类并集中维护源码清单。应用继续使用公开 SDK API；固定 GPIO/TIM/DMA 宏不再通过蜂鸣器/WS2812 公共头开放。指示器运行策略位于 Services/Indicators。修改硬件接线、设备功能或服务行为前，参考 [职责审查](Docs/BoardSupport.md)。

## SystemMonitor

默认 `system_monitor_enabled=true`，Low/4 KiB 的 SystemMonitor 替代原 Diagnostics，1 Hz 发布 ADC 五参数和全线程基础统计；Indicators 接入统一框架，行为与节奏保持。按槽短临界区取样，默认不扫栈。原 `diagnostics_enabled` 配置和 HSKP 公共入口删除；HostLinkPrepareEx 不再接收 stack_scan 参数。公共快照、物理量系数、有效性和板测边界见 [SystemMonitor](Source/Services/SystemMonitor/README.md)。

## 2026-09-28 板端验证

托管任务与 SystemMonitor 已完成用户授权的烧录及板端验证，修正 BDMA 完成后的停止判断与主机 SAMPLE 消息号，并缩减循环 hook 开销。实测环境、结果和未验收范围见 [板端记录](Docs/Validation/ManagedTasksBoard-20260928/README.md)。

本轮持久化/高频变量的主机、公共边界、父项目与资源验证见 [Persistence-20260928](Docs/Validation/Persistence-20260928/README.md)；独立板测入口在 [Tests/Board/Persistence](Tests/Board/Persistence/README.md)，尚未烧录或执行真实断电/组合负载。
