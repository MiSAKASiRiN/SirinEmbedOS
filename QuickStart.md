# SirinEmbedOS QuickStart

从构建 SDK 到创建一个周期任务。构建命令只生成文件，不连接或烧录 PCB；详细接口查 [文档导航](Docs/README.md)。

## 准备工具

本指南使用 CMake 3.24 或更高版本（SDK 工程最低版本为 3.22）、Ninja、Python 3，以及 ST Arm Clang / Picolibc。当前构建配置使用 STM32CubeCLT 的 ST Arm Clang，不直接替换成 GCC/Newlib。以下安装路径按本机调整：

```sh
export PATH="/opt/ST/STM32CubeCLT_1.22.0/st-arm-clang/bin:$PATH"
starm-clang --version
cmake --version
ninja --version
```

## 构建 SDK 示例

从 `SirinEmbedOS/` 根目录执行：

```sh
cmake --preset Debug -DSIRINEMBEDOS_EXAMPLE=Minimal -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build --preset Debug -j 8
```

产物是 `build/Debug/SirinEmbedOS.elf` 和 `.map`。Release 将两处 `Debug` 换为 `Release`。更换工具链后用 `cmake --fresh --preset Debug -DSIRINEMBEDOS_EXAMPLE=Minimal -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` 重新配置。CLion 打开 SDK 根目录；开发父项目时打开父项目根目录。

Minimal 应用只创建心跳任务，但平台默认传感器、INS、温控和存储加载仍会启动。上板前核对 [Minimal](Examples/Minimal/README.md)及应用配置。下方父项目示例显式关闭融合和加热。

## 创建父项目

把 SDK 子模块或现有检出目录放在父项目中：

```text
MyRobot/
├── CMakeLists.txt
├── App/app_main.cpp
└── SirinEmbedOS/
```

`CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.22)
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/SirinEmbedOS/CMake/starm-clang.cmake")
project(MyRobot LANGUAGES C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_subdirectory(SirinEmbedOS)
sirinos_add_firmware(MyRobot App/app_main.cpp)
```

`App/app_main.cpp`，这是完整源文件：

```cpp
#include <SirinEmbedOS/Kernel/Startup.h>
#include <SirinEmbedOS/Kernel/Thread.h>

volatile uint32_t app_heartbeat = 0;
volatile SRNOS_Result app_create_result = SRNOS_BUSY;
static SRNOS_Thread heartbeat_task = 0;

extern "C" void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config)
{
    // 调度器初始化之前执行，只声明配置。
    config->ins.enabled = false;
    config->sensors.heater_enabled = false;
    config->load_parameters = false;
}

static SRNOS_StepResult heartbeat_step(void *, const SRNOS_ThreadWake *)
{
    ++app_heartbeat;
    return SRNOS_STEP_CONTINUE;
}

extern "C" void SRNOS_ApplicationMain(void)
{
    SRNOS_ThreadConfig task{};
    task.name = "Heartbeat";
    task.step = heartbeat_step;
    task.stack_bytes = 4096;
    task.priority = 24;
    task.mode = SRNOS_TASK_PERIODIC;
    task.period_ms = 1000;
    app_create_result = SRNOS_ThreadCreate(&task, &heartbeat_task);
    if (app_create_result != SRNOS_OK) return;
    // 入口返回后，Heartbeat 继续由 SDK 调度。
}
```

从父项目根目录执行：

```sh
cmake -S . -B build/Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/Debug -j 8
```

产物为 `build/Debug/MyRobot.elf` 和 `.map`。SDK 持有唯一的 `main()` 和调度器入口；不要再次定义或启动，也不要补入 Board、Source、HAL、CMSIS、FreeRTOS 的私有 include 路径。

上板后观察 `app_create_result == SRNOS_OK`、`app_heartbeat` 每秒递增。任务创建成功不代表所有外设正常。该示例关闭融合、加热和业务参数加载，基础传感器任务及身份/校准启动读取仍按平台配置执行。

## 检查任务和内存

在应用任务中读取。以下为可并入任务的 C++ 函数片段：

```cpp
#include <SirinEmbedOS/Kernel/Thread.h>

static SRNOS_KernelResources app_resources{};
static SRNOS_ThreadInfo app_task_info{};

static bool sample_resources()
{
    if (SRNOS_KernelGetResources(&app_resources) != SRNOS_OK) return false;
    return SRNOS_ThreadGetInfo(SRNOS_ThreadGetCurrent(), &app_task_info) == SRNOS_OK;
}
```

- `kernel_free_bytes` / `kernel_minimum_free_bytes`：RTOS 堆当前余量 / 启动以来最小余量。
- `capacity_bytes` / `used_bytes` / `peak_bytes`：应用对象池容量 / 占用 / 峰值；池本身来自 RTOS 堆，不能重复计数。
- `stack_bytes`：任务配置的栈容量，**不是实际栈余量**。常规 SystemMonitor 不扫描任务栈；专用栈观测会扰动调度。

由另一管理任务调用 `SRNOS_ThreadDelete(handle, timeout_ms)` 停止并回收任务，成功后才释放参数或清空句柄；超时保留资源。不能同步删除自身。完整生命周期见 [Kernel](Source/Kernel/README.md)。

## 配置和外设

CAN/UART/IMU、网络与服务开关在 `SRNOS_ApplicationConfigure()` 中声明并冻结。编译期预算可在 `add_subdirectory(SirinEmbedOS)` 前指定一个已有的配置头：

```cmake
set(SIRINEMBEDOS_CONFIG_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/Config/sdk_config.h"
    CACHE FILEPATH "Application SDK configuration")
```

`Config/sdk_config.h` 示例保持默认对象池大小：

```c
#pragma once
#define SRNOS_APP_OBJECT_POOL_BYTES (32U * 1024U)
```

配置头必须存在且使用绝对路径；同一构建内全部 SDK 对象共用该配置。调整堆、栈或容量后检查 map 和运行水位。

| 功能 | 接入方式与示例 |
|---|---|
| CAN 电机 | [Motors](Source/Devices/Motors/README.md)建立对象，配置阶段 Attach；[CAN](Source/IO/CAN/README.md)检查通道与模式。 |
| UDP / Ping | [Network](Source/Services/Network/README.md)同时启用 Ethernet 和 Network、声明地址与端口，再由任务收发。 |
| NTP 授时 | 配置 `network.ntp` 的服务器与周期；任务通过 [Clock](Source/Services/Clock/README.md) 读取 uptime/Unix 时间，先检查 `absolute_valid`。 |
| NOR 黑匣子 | 设置 `blackbox.enabled` 和队列深度并核对启动 heap 预算；通过 [Blackbox](Source/Services/Blackbox/README.md) 的 Append/异步读接口使用 SDK 专用线程。 |
| 通用输入与消息 | [InputMessages](Examples/InputMessages/README.md)：各来源独立通道/按键/增量快照，SirinRMT 支持 HostComm/UART/UDP/图传，自定义消息按服务分发。 |
| 遥控 / 串口 | [Serial](Source/IO/Serial/README.md)声明端口用途、波特率及协议绑定，避免所有权冲突。 |
| 姿态 | [INS](Docs/INS.md)声明安装旋转，消费者检查 VALID、序号及 epoch。 |
| 参数 / 遥测 | [Parameters](Examples/Parameters/README.md)注册业务数值；[Telemetry](Examples/Telemetry/README.md)发布只读变量。 |
| 图传遥测 | [VideoLink](Source/Services/VideoLink/README.md)：先在 ApplicationConfigure/静态初始化注册 Telemetry，再在 bind_channels 绑定图传周期；打开 VT03 UART 的 TX 和 video_link_enabled。 |
| USB 上位机 | CDC0 为日志、CDC1 为 SirinLink；[采集工具](Tools/SirinLink/README.md)显式选择实际 CDC1 端口。 |

## 离线检查与常见问题

构建步骤见上文；维护与验收边界见 [开发指南](Docs/Development.md)。

| 现象 | 检查方式 |
|---|---|
| 找不到编译器 / Ninja | 检查 PATH，重新配置 CMake。 |
| 提示使用 SDK 工具链 | 使用 `CMake/starm-clang.cmake`，不直接使用 `Board/cmake/starm-clang.cmake`，清理旧工具链缓存。 |
| 应用找不到 HAL/RTOS 头 | 改用 SDK 公共 API，常规父项目不补私有 include 路径。 |
| ThreadCreate 返回 RESOURCE | 检查应用对象池、数量上限和创建顺序。 |
| RUNNING 但设备无数据 | 检查设备状态、数据年龄和有效位；RUNNING 只表示系统启动阶段。 |
| 关闭 INS 后仍有相关任务 | 融合开关不裁剪基础采样和 INS worker；加热由独立配置项控制。 |

修改历史与验证结果只写对应 `CHANGELOG.md`；使用说明维护最终行为和示例。
