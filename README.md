# SirinEmbedOS

面向固定 STM32H723 / OmniCtrl 主控的 SDK。应用通过一个 Git 子模块引入整个 SDK，自行管理控制算法、业务线程与配置；SDK 内不保留原 Framework 的嵌套子仓库。CubeMX 只管理硬件与 HAL，调度和系统服务由 SDK 管理。目录职责见 [AGENTS.md](AGENTS.md)。

目前已将 prototype 的基础操作、Fault、内存、诊断、CAN/SPI/I2C、固定设备、串口协议、日志、参数、HostLink 和双 CDC USB 能力纳入源码与显式构建清单。公共基础 API 使用 `SRNOS_*` / `SRNOS::`；迁入模块保留的协议/API 名称以各公共头为准，不提供更名前的兼容别名。模块去向和未迁入项见 [迁移清单](Docs/Migration/README.md)。FatFs、LwIP 尚未接入。

已完成主机测试和固件编译链接。用户首次板端调试确认可进入 RUNNING 并采到 IMU 数据，同时发现启动事件屏障误判导致复位循环；源码已修复，随后直接连接及一次软件复位复测未再现旧断言，IMU 数据持续更新；完整外设和压力验收仍待完成。当前验证结果和待测项见 [验证记录](Docs/Validation.md)。

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

也可自行创建 executable 并链接 `SirinEmbedOS::Platform`；helper 另外设置 `.elf` 后缀和按目标命名的 map。Platform 是完整链接入口，包含 Board、HAL、Kernel、组件对象、启动与链接配置。SDK 不重置父项目名或工具链，作为子目录时默认不创建示例固件。

应用实现以下公开入口，不再定义另一个 `main()`：

```cpp
#include <SirinEmbedOS/Kernel/Startup.h>
#include <cmsis_os2.h>

void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config)
{
    // 仅启动声明；此时调度器尚未初始化。
    config->application_stack_bytes = 8192;
    config->load_parameters = false;
}

void SRNOS_ApplicationMain()
{
    for (;;) {
        // 应用控制算法、状态机和业务循环放在父项目。
        osDelay(1000);
    }
}
```

`Examples/Minimal` 验证公开启动入口；[BringUp](Examples/BringUp/README.md) 提供首次上板的启动/堆/任务栈/IMU 观测；[SensorBringUp](Examples/SensorBringUp/README.md) 验证 2613 原始传感器与共享总线，关闭融合和 EEPROM，默认使用统一60°C温控，并提供 [CDC1 双 IMU 遥测 CLI](Docs/HostLink.md)；[Parameters](Examples/Parameters/README.md) 展示显式业务 PID 参数和一致快照；[PrototypeBindings](Examples/PrototypeBindings/README.md) 展示 prototype 的 DR16、VT03、裁判与日志端口映射。选择一个应用入口，可按说明组合串口绑定文件。迁移收尾顺序见 [后续开发清单](Docs/Development.md)。

## 配置与生命周期

启动顺序为专用段/Fault 早期初始化 → 生成的 HAL 初始化 → 应用配置与服务声明 → 全量校验、注册表及系统配置冻结 → `osKernelInitialize()` → 创建系统与应用线程 → 唯一一次 `osKernelStart()`。调度后按冻结配置决定是否加载业务参数（默认关闭），再启动 USB，最后放行应用入口和配置中声明的应用线程。应用入口允许返回，返回时仅退出该线程。

CAN、UART、IMU、服务选择、资源预算和绑定均在初始化时冻结。配置 hook 不做阻塞 I/O，不创建线程；新增应用线程通过 `SRNOS_SystemConfig::threads` 声明。`bind_channels` 在内置协议完成声明后用于原始串口、H12H 或 RS485 设备的所有权登记。没有运行时重绑定、端口切换或系统配置重载。

默认集成三路原生 CAN、外置 CAN、板载 IMU、EEPROM、板级指示器和诊断；USB 默认提供 CDC0 日志与 CDC1 HostLink。普通 USART1/USART6/UART8/UART9 默认未绑定，不会自动占用遥控或日志用途。UART7 是板载固定 RS485 通路，外接设备仍由应用显式声明。H12H 默认未绑定。默认包含总线能力不代表上电自动使能电机或执行运动。

Serial 统一拥有 HAL 收发、DMA 缓冲与回调。端口只能被一个 owner 占用；协议、原始接口和日志不可绕过占用管理并发使用同一端口。`GetChannel()` 仅在初始化时返回未占用通道，成功声明后由所有者保留该指针；配置了但未声明 owner 的端口会阻止启动。具体缓冲、超时、线程/ISR 限制以公开接口为准。

参数表用于应用明确声明的业务数值，不用于系统结构配置。`Param<T>` 默认只读；允许主机写入的值必须显式使用 `PARAMS_MUTABLE_BUSINESS` 并声明范围。注册表和访问权限冻结后，业务数值仍可更新；控制循环用 `params_snapshot()` 取得调用者拥有的一致副本。默认 `load_parameters=false`、`host_link_storage_enabled=false`，参数持久化仍依赖旧 24C02，尚未迁移至新器件；业务数值的显式注册/读写仍可使用。详见 [服务合同](Source/Services/README.md)。

2613 的 FT24C128A 已提供独立的 [阻塞/非阻塞器件 API](Source/Devices/EEPROM/README.md)，完成 16 KiB 整片交叉读写、边界和备份恢复板测；使用 `SIRINEMBEDOS_EXAMPLE=EEPROMBringUp` 构建 [专用验证应用](Examples/EEPROMBringUp/README.md)。本轮未迁移参数/INS 上层存储。[I2C4 分发线程](Source/IO/I2C/README.md) 为 LIS/SPA 保留周期槽，EEPROM 读写/校验按 32 B 分段。整片压力下两传感器持续 READY，LIS 约 88–93 Hz、SPA 约 16 Hz，无总线错误或超时；这是正常并发短测，未覆盖掉电/断线，见 [板测记录](Docs/Validation/I2C-20260928/README.md)。

`SRNOS_SystemGetStatus()` 反映启动阶段、原生 CAN 初始化、USB 启动及参数加载结果；`RUNNING` 不是全部设备健康证明。USB 启动成功不等于主机已枚举或打开 CDC，外置 CAN 和 IMU 在各自 worker 中初始化，需要检查对应状态与诊断；应用必须自行决定未就绪时的业务行为。

## 内存与内核合同

父项目配置头可覆盖 `SRNOS_APP_STACK_BYTES`、`SRNOS_APP_PRIORITY`、`SRNOS_HEAP_DTCM_BYTES`、`SRNOS_HEAP_D1_BYTES`，默认分别为 4096 字节、CMSIS Normal（24）、64 KiB、128 KiB。配置 hook 可进一步声明初始应用线程预算。`SRNOS_SystemHeapBudget()` 提供保守启动预算校验，应用运行期额外分配仍需自行预留容量。不同 SDK 配置使用不同构建目录。

- FreeRTOS 10.6.2 / CMSIS-RTOS2 / heap_5 固定快照随 SDK 管理，Kernel tick 为 1 kHz；HAL tick 使用 TIM7。SVC/PendSV/SysTick 由 FreeRTOS/CMSIS 持有；调用 FromISR API 的 IRQ 数值优先级必须在 5～15。
- 普通 data/TLS/BSS 放在 D1，DTCM 保留热数据、heap_5 首区和 MSP。默认 MSP 与独立 libc arena 各 8 KiB，可通过同名 CMake 缓存项 `SRNOS_MSP_BYTES` / `SRNOS_LIBC_BYTES` 调整。libc arena 与 RTOS heap 分离。
- DMA 专用段在非缓存 D2，BDMA 缓冲在 D3。NOLOAD DMA/BSS/Watch 段由早期启动显式初始化；Fault retained 区不清零。普通 heap_5 分配不是 DMA 分配器，payload 不保证清零。
- Picolibc TLS 占用线程栈，栈预算须包含 TLS。libc 锁由静态递归 RTOS mutex 接入，ISR 不可调用分配器或阻塞 stdio；该适配不代表任意共享 `FILE` 操作都线程安全。
- Fault 默认记录 RAM 并按策略 halt/reset，不抢占或复位 USART1。紧急输出只能在配置阶段显式预留；正常日志走自身队列和 Serial/USB sink。

具体内存段、异常和锁规则见 [基础层说明](Source/Core/README.md)。各第三方目录保留来源、许可证和哈希；AL94 1.0.3 仅纳入 Core、COMPOSITE、CDC_ACM，双 CDC class 使用静态存储。

## 验证

```sh
python3 Tests/run_host_tests.py
python3 Tests/Build/check_structure.py
python3 Tests/Build/check_sdk.py
```

构建回归检查父项目九种固件接入（含三个 BringUp）、公共头联合编译、配置覆盖、第三方哈希、链接符号和内存布局；当前 19 组能力主机测试位于 `Tests/`。命令、结果与硬件待测项集中在 [验证记录](Docs/Validation.md)。编译、静态检查和主机仿真不替代 USB 枚举、UART DMA/IDLE/错误恢复、CAN 收发、IMU 采样、Cache、Fault 和实际调度验证。

## 正式 INS

默认启用双六轴 Mahony INS 与60°C恒温控制，固定板内旋转、启动冻结的业务安装四元数、多消费者快照和降级状态见 [INS合同](Docs/INS.md)。`SIRINEMBEDOS_EXAMPLE=INSBringUp` 提供无周期栈扫描的观测入口；原始对比用 SensorBringUp。旧单BMI融合/校准/温控路径已删除，默认不自动访问参数存储。
