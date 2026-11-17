# SirinEmbedOS QuickStart

本指南使用 OmniCtrl 2 Pro Rev.C（2613～2616，STM32H723VGH6）。先编译 SDK 自带的 Minimal，再构建一个父工程，把独立 C 文件中的周期任务注册到 SDK。配置电机、VT03、UART 或自定义协议时，可直接查 [初始化配置指南](Docs/Config.md)。

## 构建 SDK 示例

准备 ST Arm Clang / Picolibc、Ninja 和 CMake 3.24 或更高版本。从 SDK 根目录运行以下命令，首行按本机 STM32CubeCLT 安装位置调整：

```sh
export PATH="/opt/ST/STM32CubeCLT_1.22.0/st-arm-clang/bin:$PATH"
starm-clang --version
cmake --preset Debug -DSIRINEMBEDOS_EXAMPLE=Minimal
cmake --build --preset Debug
```

生成 `build/Debug/SirinEmbedOS.elf` 和同目录的 `.map`。Release 使用同名 Release preset。CLion 打开 SDK 根目录；构建命令只生成文件。Minimal 创建一个心跳任务，并沿用板载传感器、融合、温控和存储的默认配置。[Minimal 源文件](Examples/Minimal/app_main.c)

## 创建父项目

在父项目根目录放入名为 `SirinEmbedOS` 的 SDK 子模块或检出目录。下面两份示例源文件可以直接复制：

```sh
mkdir -p App/Tasks
cp SirinEmbedOS/Examples/StartupTask/app_main.cpp App/app_main.cpp
cp SirinEmbedOS/Examples/StartupTask/heartbeat_task.c App/Tasks/heartbeat_task.c
```

在父项目根目录创建 `CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.22)
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/SirinEmbedOS/CMake/starm-clang.cmake")
project(MyRobot LANGUAGES C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_subdirectory(SirinEmbedOS)
sirinos_add_firmware(MyRobot App/app_main.cpp App/Tasks/heartbeat_task.c)
```

从父项目根目录构建：

```sh
cmake -S . -B build/Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/Debug
```

产物为 `build/Debug/MyRobot.elf` 和 `MyRobot.map`。SDK 提供板级 `main()`、调度器和硬件初始化。应用源文件只包含 `SirinEmbedOS/` 公共头。

## 在独立文件中注册一个 Task

打开刚复制的 [heartbeat_task.c](Examples/StartupTask/heartbeat_task.c)。这个文件包含任务所需的全部内容：私有计数器、每次只执行一轮的 `heartbeat_step`，以及 `AppRegisterHeartbeatTask(config)`。注册函数先检查列表容量，再把名为 `Heartbeat`、周期 1000 ms、栈 4096 字节的任务描述追加到 `config->threads[]`。

在 [app_main.cpp](Examples/StartupTask/app_main.cpp) 中，C++ 入口通过 C 链接声明找到注册函数：

```cpp
#include "SirinEmbedOS/Core/C/Fault.h"
#include "SirinEmbedOS/Kernel/Startup.h"
#include "SirinEmbedOS/Services/Logging/Log.h"

extern "C" bool AppRegisterHeartbeatTask(SRNOS_SystemConfig *config);

extern "C" void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config)
{
    config->ins.enabled = false;
    config->sensors.heater_enabled = false;
    SRNOS_ASSERT(AppRegisterHeartbeatTask(config));
}

extern "C" void SRNOS_ApplicationMain(void)
{
    (void)SRNOS_LOGI_SRC("App", "Application started");
}
```

SDK 在配置冻结后创建列表中的任务。上板后可在调试器观察 `heartbeat_count` 每秒递增。任务可能先于 `SRNOS_ApplicationMain()` 执行，所需参数应提前准备好。需要让任务等待某个运行期资源，或需要保存句柄以便停止、等待和删除时，可在应用线程内创建它，见 [运行期 ThreadCreate 示例](Examples/Minimal/README.md)。为同一项任务选择一种创建方式。

## 日志、遥测和 EEPROM 参数

| 目的 | 父项目写什么 | 主机看到什么 |
|---|---|---|
| 输出文本日志 | 在应用任务中用 `SRNOS_LOGI_SRC("App", "Application started")` 等宏。默认后端为 USB CDC0。 | CDC0 是文本串口。日志成功入队后由后台发送；可用 `srneos log --cdc0 CDC0_PATH --duration 30` 观察。[日志服务](Source/Services/Logging/README.md) |
| 发布只读测量值 | 启动时声明 `SRNOS::Telemetry::Value<T>`，采样后调用 `Publish(value, sampled_us)`，采样时间取自 `SRNOS_TimeUs()`。 | 在 CDC1 上通过 SirinLink 发现和订阅最新值。该值只读，保存在 RAM。[Telemetry 示例](Examples/Telemetry/app_main.cpp) |
| 允许调参、断电后恢复 | 声明 `SRNOS::Parameters::Persistent<T>`，给定稳定 ID、名称、默认值和范围，在配置回调设置应用 ID 和兼容版本。关联参数在业务周期边界读取一致快照。 | WRITE 修改 RAM，可用 `srneos parameters persist --cdc1 CDC1_PATH` 显式将快照保存到板载 EEPROM。启用启动加载后，SDK 在放行业务线程前尝试恢复；空白或无效记录使用声明默认值。[参数完整示例](Examples/Parameters/app_main.cpp) · [保存与加载](Docs/Storage.md) |

主机命令中的 `CDC0_PATH`、`CDC1_PATH` 换为实际设备路径。安装 `srneos` 后，可使用 `srneos connect usb --list` 找到端口，使用 `srneos telemetry list --cdc1 CDC1_PATH` 查看遥测目录。C 应用也可调用 `SRNOS_TelemetryRegister()` 和 `SRNOS_ParametersRegisterPersistent()` 进行注册。[主机 CLI](Tools/CmdLineTool/README.md)

## 配置和外设

优先接入 **VT03/VT13** 时，先给所接 UART 配置 921600、8N1、RX，设置 `remote.vt03_port`，再从任务读取 `vt03` 快照。回传自定义数据或遥测时再启用 TX 和 VideoLink。CAN 电机、原始 UART、自定义协议、编译期容量及启动回调的先后顺序均在 [初始化配置指南](Docs/Config.md)；VT02/VT12 使用自己的 [接入指南](Docs/VT02VT12.md)。

## 检查任务和内存

在任务中用 `SRNOS_KernelGetResources()` 读取 RTOS 堆余量、历史最小余量和应用对象池占用；用 `SRNOS_ThreadGetInfo()` 查看任务状态和配置栈大小。启动预算、运行堆余量、实际栈水位分别反映不同约束。系统进入 RUNNING 后，设备在线、样本有效和 USB 枚举仍需分别检查。[Kernel 示例](Source/Kernel/README.md#运行期内存观测示例) · [开发与验证](Docs/Development.md)
