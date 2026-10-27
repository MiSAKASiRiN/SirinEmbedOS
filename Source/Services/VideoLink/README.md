# 图传自定义数据与遥测

公共接口为 `SirinEmbedOS/Services/VideoLink.h`。服务复用 [Telemetry](../Telemetry/README.md) 的注册表和最新值缓存，以 A5 `0x0310` 发送固定 300 字节，以 `0x0311` 接收固定 30 字节。VT03 与 A5 共用一个 Serial RX 所有者；不创建新任务，不操作电机，不把自定义数据映射为遥控输入。

## 启用与注册

在 `SRNOS_ApplicationConfigure` 中配置 VT03 通道为 921600/8N1、RX/TX 全双工，设置 `config->remote.vt03_port` 和 `config->remote.video_link_enabled=true`。默认关闭。在线路上连接 MCU TX → 接收机 RX、接收机 TX → MCU RX 及共地。

在 `SRNOS_ApplicationConfigure`（或静态初始化）中注册 Telemetry 字段，早于 SirinLink 准备时冻结目录。在稍后的 `config->bind_channels` 回调中绑定图传路由，此时 Serial/RemoteControl 已准备好，VideoLink 路由尚未冻结：

```c
#include <SirinEmbedOS/Services/VideoLink.h>
static SRNOS_TelemetryHandle angle;

static bool register_telemetry(void) {
    const SRNOS_TelemetryDescriptor d = {
        "motor.angle", "deg", SRNOS_TELEMETRY_FLOAT32, 10
    };
    return SRNOS_TelemetryRegister(&d, &angle) == SRNOS_TELEMETRY_OK;
}

static bool bind_telemetry(void *user) {
    (void)user;
    return SRNOS_VideoLinkRegisterTelemetry(angle, 1, 100, 500) == SRNOS_VIDEO_LINK_OK;
}
```

业务任务读取自己的设备快照后调用 `SRNOS_TelemetryPublish(angle, value, sampled_us, valid)`，`sampled_us` 使用 `SRNOS_TimeUs()` 时间域。服务按注册周期自动发送最新值。注册不会自动读取设备：电机类型、角度含义和有效性仍由父项目定义。

每个路由需要唯一 handle 和非零 wire ID，最多 32 项；周期 25..60000 ms 且为 25 的倍数，新鲜度上限 20..60000 ms。名称、单位、类型来自已有 Telemetry 描述。注册在调度器启动前冻结，不支持运行期解绑。Telemetry 的 `max_hz` 是 SirinLink 订阅上限，图传周期独立设置；例如温度描述 `max_hz=5`，图传周期 1000 ms。

## 调度、接收与资源

- 同周期值聚合，每包最多 17 项。全通道最多 40 包/s（25 ms 间隔，为协议 50 Hz 上限留出转发余量），包含样本、描述、ACK 和手动 Send；共享限速不允许多任务绕过。
- 优先发送到期样本，其次 ACK，最后描述。描述每 5 s 重播一轮，也可主动请求。高负载下 ACK/目录可能延后；注册总负载应留出余量。
- 发送非阻塞，Serial 忙时保留到期状态；恢复后发送最新值并跳过错过周期，不补发历史积压。无线链路不保证送达，无自动可靠重传。
- 离线、未发布、未来时间戳或样本超龄以 `valid=0` 发送；接收端也必须检查本地接收时效。
- 非保留 `0311` 交给 [Message](../Message/README.md)：SRMT 是 SirinRMT 原生载荷，SM 是最多20 B的通用消息载荷，各业务订阅自己的 service/type。无路由/拒绝时累加 rx_dropped；rx_messages 统计收到的0311。保留 SRVL 请求由服务处理，不刷新物理遥控在线状态。
- `SRNOS_VideoLinkSend` 复制调用方恰好 300 字节；返回 OK 仅表示进入 Serial 队列。`GetStats` 提供计数，所有运行期 API 仅限任务上下文。
- 使用 InputIO 的3072 B栈和优先级47。VideoLink 保留固定遥测路由和包缓冲，接收存储由 Message 的启动期队列统一管理。A5发送临时栈帧309 B；注册满额、长期空口拥塞和不同接线应分别验证。

## SRVL v1 载荷

所有整数和 float32 位模式均小端，保留位和尾部填零。A5 外层使用标准 CRC8/CRC16；`0310` 数据恰好 300 字节。

| 偏移 | 大小 | 内容 |
|---|---:|---|
| 0 | 4 | ASCII `SRVL` |
| 4 | 1 | 版本 1 |
| 5 | 1 | 类型：1 样本、2 描述、3 ACK |
| 6 | 1 | 项数：样本 1..17、描述 1、ACK 0 |
| 7 | 1 | 保留 0 |
| 8 | 4 | 发送包序号，uint32 回绕 |
| 12 | 4 | MCU 观测时钟毫秒低 32 位 |

样本从偏移 16 开始，每项 16 字节：`id:u16, type:u8, valid:u8, sampled_ms:u32, publication_sequence:u32, value_bits:u32`。类型沿用 Telemetry：1=float32、2=int32、3=uint32、4=bool。传输序号与发布序号不同；一次发布可多次传输。

描述从偏移 16 开始：`id:u16, type:u8, name_length:u8, unit_length:u8, reserved:u8, period_ms:u16, max_age_ms:u32`；偏移 28 后紧跟名称和单位 UTF-8 字节，不附 NUL。名称 1..63 字节、单位 0..15 字节。接收端按 ID 合并描述与样本。

`0311` 保留请求为 30 字节：`SRVL`、版本 1、操作码（`0x80` 请求目录，`0x81` Ping）、两个零字节、`request_id:u32`、18 字节调用方内容。ACK 偏移 16..45 原样回传完整 30 字节，供端到端匹配；操作 `0x80` 同时重新安排目录。其他请求交给 FIFO，不作为运动命令。

无认证、加密或可靠事务语义；有副作用的业务命令需父项目另行定义权限、时效和幂等性。当前服务只提供数据通路。

变更与验证边界见 [CHANGELOG](CHANGELOG.md)。
