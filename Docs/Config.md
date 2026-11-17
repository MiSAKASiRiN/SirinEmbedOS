# 初始化配置

父项目在一处实现 `SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config)`。进入回调时，SDK 已经填入本板默认配置。应用只覆盖自己使用的字段：电机接哪一路 CAN、各 UART 接什么设备、启用哪些协议、注册哪些业务任务。固定板卡的 HAL、DMA、IRQ 和 RTOS 初始化由 SDK 完成。父项目构建模板见 [QuickStart](../QuickStart.md#创建父项目)。

## 什么时候注册

| 阶段 | 父项目在这里做什么 |
|---|---|
| 静态初始化或 `SRNOS_ApplicationConfigure` | 声明只读遥测和持久化业务参数。配置回调内设置 CAN 模式、串口格式、服务开关和固定任务列表，并可注册 CAN 电机适配器。 |
| `config->bind_channels` | SDK 已按配置创建串口通道，内置协议已取得各自的所有权。此时可获取尚未占用的通道并 Claim，初始化自有协议或 RS485 端点，登记 Message 订阅、VideoLink 遥测路由。返回 `false` 会阻止启动。 |
| `config->sirin_link_setup` | 启用 SirinLink 时，SDK 在内置路由后调用此回调，可登记父项目命令、handler 和发送主题。此时 Input 与 Message 的目录已经冻结。回调返回注册结果。 |
| 调度启动后 | SDK 尝试加载启动参数并启动已启用的 USB，然后放行启动列表中的任务与 `SRNOS_ApplicationMain()`。应用在任务中执行收发、读取反馈、发布遥测；需要管理句柄时可在此创建任务。 |

前三阶段只做声明与注册，避免阻塞 I/O 或创建线程。电机对象、回调参数和启动任务名须在使用期间有效。SDK 会校验通道模式、协议冲突及启动资源预算，再冻结配置。具体顺序见 [Kernel](../Source/Kernel/README.md#输入与消息启动顺序) 和 [SirinLink 注册顺序](../Source/Services/SirinLink/README.md#启动注册与容量)。

## 示例：CAN1 电机、UART9 遥控和 USART6 消息

下面是一个完整的 C++17 配置文件 `App/config.cpp`。例中设备号 1 的 C620 接 CAN1，VT03 接 UART9，另外用 USART6 收发父项目自定义 Message。端口按实际接线选择。读取 VT03 遥控只需 RX；USART6 上的 Message 协议需要 8N1、RX/TX 全双工。

```cpp
#include <SirinEmbedOS/Core/C/Fault.h>
#include <SirinEmbedOS/Devices/Motors/C620/C620.hpp>
#include <SirinEmbedOS/Kernel/System.h>
#include <SirinEmbedOS/Services/Message.h>

static SRNOS::Devices::DJI_C620 motor{1};
static SRNOS_MessageRoute status_route;

static bool bind_routes(void *)
{
    SRNOS_MessageRouteConfig route{};
    route.transports = SRNOS_MESSAGE_UART;
    route.endpoint = SRNOS_SERIAL_USART6;
    route.service = 0x40;
    route.type = 1;
    route.max_payload = 20;
    route.max_age_ms = 1000;
    route.depth = 1;
    route.policy = SRNOS_MESSAGE_LATEST;
    return SRNOS_MessageRegister(&route, &status_route) == SRNOS_MESSAGE_OK;
}

void AppConfigureDevices(SRNOS_SystemConfig *config)
{
    config->can[0] = {SRNOS_CAN_MODE_CLASSIC, 1000000u, 0u, false};
    motor.set_give_cmd(0);
    SRNOS_ASSERT(SRNOS::Devices::DJI_C620_AttachCan(
        SRNOS_CAN_CH_NTFDCAN1, &motor, 2) == 0);

    SRNOS_SerialConfig remote{};
    remote.port = SRNOS_SERIAL_UART9;
    remote.baud_rate = 921600;
    remote.data_bits = 8;
    remote.stop_bits = 1;
    remote.parity = SRNOS_SERIAL_PARITY_NONE;
    remote.mode = SRNOS_SERIAL_DUPLEX;
    remote.rx_enabled = true;
    config->remote.vt03_port = remote.port;

    SRNOS_SerialConfig messages = remote;
    messages.port = SRNOS_SERIAL_USART6;
    messages.baud_rate = 115200;
    messages.tx_enabled = true;
    config->messages.uart_port = messages.port;

    config->serial[0] = remote;
    config->serial[1] = messages;
    config->serial_count = 2;
    config->bind_channels = bind_routes;
}

SRNOS_MessageResult AppReadStatus(SRNOS_MessageInfo *info, uint8_t *data, size_t capacity)
{
    return SRNOS_MessageReceive(status_route, info, data, capacity);
}
```

**并入 QuickStart**：在父项目的 `sirinos_add_firmware(...)` 源文件清单加入 `App/config.cpp`。在 `App/app_main.cpp` 声明 `void AppConfigureDevices(SRNOS_SystemConfig *);`，并在已有的 `SRNOS_ApplicationConfigure` 中调用 `AppConfigureDevices(config);`。这个例子负责完整的两项串口表；已有端口或绑定回调的项目应合并条目和回调，保留原有所有者。

- **CAN 电机**。C620 的构造参数 `1` 是电调设备号，该型号对应反馈 ID `0x201`。Attach 的第一个参数选择 CAN1，最后一个参数 `2` 是发送周期（ms）。注册成功后 Router 会周期发送同组电机的命令，即使初始命令为零。运行期用 `motor.snapshot(state)` 读取反馈并检查 `state.online` 和时间戳。各电机型号对应的标识符、单位和分组方式见 [电机适配](../Source/Devices/Motors/README.md)。
- **VT03/VT13**。SDK 取得 UART9 后自动注册名为 `vt03` 的 Input 来源。任务用 `SRNOS_InputFind("vt03")` 和 `SRNOS_InputReadSnapshot()` 读取摇杆、档位、键鼠。需要双向图传时，在复制串口配置进数组前设 `remote.tx_enabled = true`，并设 `config->remote.video_link_enabled = true`；遥测路由可在同一个 `bind_routes` 中注册。[VT03 快照示例](../Source/Services/RemoteControl/README.md#vt03vt13-接入) · [VideoLink 注册示例](../Source/Services/VideoLink/README.md#启用与注册)
- **自定义协议**。例中的 USART6 由 Message 服务占用，对端发送 V2 COBS 格式的 EVENT，service/type 为 `0x40/1`。任务调用 `AppReadStatus`，按 `SRNOS_MESSAGE_OK`、`SRNOS_MESSAGE_EMPTY` 等结果处理；成功时 `info->length` 是实际字节数。若业务有自己的线格式，可改为在 `bind_channels` 里获取并 Claim 原始通道，由自己的任务解析。两种方式各自独占端口。[Message 线格式](../Source/Services/Message/README.md#传输与协议) · [原始 UART 完整绑定](../Source/IO/Serial/README.md#原始-usart6-接入示例)

## 其他常用配置

| 设备或协议 | 配置要点 |
|---|---|
| DR16 / WFLY W.Bus | 选择 RX 端口并配置 100000、8E2，设置 `remote.dr16_port` 或 `remote.wbus_port`。RX 反相按接收机输出和板上电路选择。[串口接线示例](../Examples/PrototypeBindings/README.md) · [W.Bus](../Source/Services/RemoteControl/WBus/README.md) |
| VT02/VT12 | 单独分配 115200、8N1、非反相 UART，设置 `remote.vt02_port`。键鼠走 Input，`0x0302` 自定义数据走 Message。[VT02/VT12 指南](VT02VT12.md) |
| 裁判 / RS485 | 裁判使用 115200、8N1、RX/TX 并设置 `referee_port`；板上 RS485 固定 UART7，用 RS485 模式并在绑定回调登记端点。[裁判](../Source/Services/Referee/README.md) · [RS485](../Source/IO/RS485/README.md) |
| 自定义 SirinLink 主机命令 | 保留 USB 和 SirinLink，设置 `config->sirin_link_setup`。在该回调调用 `SRNOS_SirinLinkRegisterExec()` 并返回结果，主机通过 CDC1 调用命令。普通 Message UART 提供 V2 EVENT 收发；完整主机命令和参数服务在 CDC1。[可编译命令注册示例](../Source/Services/SirinLink/README.md#父项目注册) |
| 日志与只读遥测 | 默认 CDC0 输出文本日志；若添加 UART 日志后端，还需分配独占的 TX 端口。只读变量登记名称、单位、类型、主机订阅频率上限，在采样任务中发布带时间戳的值。[Logging](../Source/Services/Logging/README.md) · [遥测示例](../Examples/Telemetry/README.md) |
| 可修改并保存的变量 | 注册带稳定 ID、默认值和范围的 `Persistent<T>`，设置非零 `parameter_application_id` 和兼容版本。保留 `storage.enabled`、`load_parameters` 以启用启动加载；通过主机保存时还需启用 USB、SirinLink 和 `sirin_link_storage_enabled`。运行期 WRITE 更新 RAM，显式 SAVE 写入 EEPROM。多项参数在同一业务周期用 `SRNOS_ParametersSnapshot()` 读取一致副本。[配置及一致快照示例](../Examples/Parameters/README.md) · [EEPROM 布局](Storage.md) |

## 编译期资源预算

串口用途和协议属于启动配置。要调整 SDK 允许覆盖的对象池、堆或默认任务栈容量，在父工程的 `add_subdirectory(SirinEmbedOS)` **之前**传入一份存在的配置头：

```cmake
set(SIRINEMBEDOS_CONFIG_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/Config/sdk_config.h"
    CACHE FILEPATH "Application SDK configuration")
```

例如 `Config/sdk_config.h` 中的 `#define SRNOS_APP_OBJECT_POOL_BYTES (32U * 1024U)` 选择了默认的应用对象池容量。同一构建内 SDK 与应用共用该配置。增加 FD 通道、消息队列或任务后，启动阶段 SDK 按注册结果复核 `SRNOS_SystemHeapBudget()`；链接 map 用于检查静态段和内存预留，运行余量通过 `SRNOS_KernelGetResources()` 读取。[Kernel 资源预算](../Source/Kernel/README.md#有界资源与预算)
