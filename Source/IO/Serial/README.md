# Serial 通道与协议接入

`Serial.h` 是公共接口；`serial_internal.h` 只供板级适配和实现使用。HAL handle、DMA、硬件 callback、RX cursor、RS485 DE/RE 均由 `Board/Support/IO/serial_board.c` 持有，协议代码只使用通道。

初始化次序为 `SerialConfigure` → 系统协议声明 → 应用 `bind_channels` → `SerialStart`。配置在 Configure 时复制并冻结；通道只能在启动前获取和 Claim。已被其他服务 Claim 或启动后的 `GetChannel` 返回空指针。应用保存自己成功 Claim 的句柄；配置但无所有者的端口阻止启动。句柄生命周期与固件相同，没有热切换或重新绑定。

任务上下文可调用 `SRNOS_SerialGetPortStats(port, &stats)` 读取已配置端口的接收字节、溢出、硬件错误、恢复次数与就绪状态，包括已由内置协议占用的端口。它复用 `GetStats` 的短临界区快照，不取得通道句柄、不消费 RX、不改变所有权；未配置端口、空输出或非任务上下文返回 `SRNOS_SERIAL_ARGUMENT`。

当前每通道使用 1024 字节 RX ring、256 字节 circular DMA RX、512 字节 DMA TX staging、4 条最大 512 字节 TX 复制队列，以及两个固定存储的二值信号量（RX 活动、TX 状态）。当前 ARM 配置每个控制块 80 B；通道表含保留 NONE 槽共六项，控制块固定占 960 B，另有句柄/恢复截止时间字段。信号量控制块位于 SDK 私有通道结构，运行时不分配，不使用父任务通知索引。RX HT 保持开启，读取实时 NDTR 而非延迟事件携带的旧位置；记录已经被 IDLE 消费的跨圈，防止随后 TC 重复交付。UART IRQ 和 DMA IRQ 在当前 IOC 中统一为 6，cursor 更新与 ring 写入仍由 FromISR 临界区串行化。超过硬件一圈保留时间的 ISR 延迟无法靠软件恢复丢失字节，需板端压力验证。

所有运行期 I/O 是任务上下文接口。Write 完整复制一包后才报告成功；Flush 等 UART TC，不以 DMA 搬运完成代替线路发送完成。Read/Write/Flush 的毫秒超时覆盖锁等待和操作本身，有限值不得超过 INT32_MAX；公开头直接提供 `SRNOS_NO_WAIT`（0，非阻塞查询/提交）和 `SRNOS_WAIT_FOREVER`（取消调用者等待期限，硬件 watchdog 仍有效）。Transceive 的 queue_timeout_ms 覆盖取锁、前序排空、静默、提交和本次 TC，response_timeout_ms 从 TC 后单独计时。Read 等待输入时释放通道锁，允许正常全双工发送；Transceive 则覆盖排队、发送、TC、响应和超时全过程，防止其他任务插入发送或取走响应。故障恢复使用冻结参数，不改变绑定。

一个公共 Serial worker（High7 / 47、2048 字节栈）处理所有端口的发送与错误恢复；协议解析留在原服务任务。RemoteControl 与 Referee 均为 High7 / 47、3072 字节栈。Referee TX 使用 Serial 共用发送队列。RS485 有 endpoint 时保留一个 High7 / 47、3072 字节栈的总线事务线程；RAW send 仍表示复制入队，空闲时分发被动 RAW 输入。线程栈包含 Picolibc TLS，以上为初始预算，板端需要结合高水位检查。

RS485 固定 UART7，使用硬件 DE，发送期间关闭 RE 抑制本机回显，UART TC 后恢复接收。Modbus 事务匹配地址、功能码、长度和 CRC，广播不等待响应；低速保持至少 3.5 字符，高于 19200 使用至少 1750 微秒静默间隔，当前 1 kHz 内核等待向上取整。队列等待与响应等待分别指定。RAW 回调得到临时数据片段，不能保存指针；超过 256 字节的连续输入会分片。

H12H 的协议实例服务多个舵机 ID，通过 Serial 事务完成命令。当前板级合同仅声明已确认的双线 UART 和 UART7 RS485；没有自动把普通 UART 改为单线的行为。设备适配器和接线需满足实际电气要求。

这不能证明实际 UART 极性、DMA 延迟、RS485 DE、USB 或设备时序已经上板通过。

## 板级边界

UART 板级接入位于 Board/Support/IO/serial_board.c，持有回调、DMA 与 RS485 电气控制。

## 托管线程

SRNOS_Serial 接入托管循环；发送入队、TC 和错误通知唤醒，空闲永久阻塞，活动 TX 按 watchdog 截止点等待。RX/Flush/事务通过状态事件和剩余截止时间等待，不按毫秒轮询；一次恢复失败后至少等待 100 ms 再尝试。 栈与资源所有权见上文；Serial、RC、Referee、RS485 统一 P1（High7 / 47），与 CAN4 同级，低于 CAN1/2/3 和 IMU/INS 的 P0（Realtime / 48）；父应用 RAW 消费线程仍由应用配置优先级。私有等待适配由 Kernel 驱动；普通等待不计入 step，处理期间 I/O/抢占/中断计入墙钟。统计不能替代板端时序验收。详见根文档及 Source/Kernel/README.md。

## 原始 USART6 接入示例

下例是一个 C 应用的配置片段，绑定 115200/8N1 的双向 USART6。若已有 ApplicationConfigure，应合并声明而不是再定义同名函数；还应确保没有遥控或其他协议占用该端口。

```c
#include "SirinEmbedOS/Kernel/System.h"
#include "SirinEmbedOS/IO/Serial/Serial.h"
static SRNOS_SerialChannel *raw_channel;
static int raw_owner;
static bool bind_raw(void *user) {
    (void)user;
    raw_channel = SRNOS_SerialGetChannel(SRNOS_SERIAL_USART6);
    return raw_channel &&
        SRNOS_SerialClaim(raw_channel, &raw_owner) == SRNOS_SERIAL_OK;
}
void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config) {
    config->serial[0] = (SRNOS_SerialConfig){
        .port=SRNOS_SERIAL_USART6, .baud_rate=115200, .data_bits=8,
        .stop_bits=1, .parity=SRNOS_SERIAL_PARITY_NONE,
        .mode=SRNOS_SERIAL_DUPLEX, .rx_enabled=true, .tx_enabled=true
    };
    config->serial_count = 1;
    config->bind_channels = bind_raw;
}
SRNOS_SerialStatus read_raw(void *buffer, size_t capacity, size_t *received) {
    return SRNOS_SerialRead(raw_channel, buffer, capacity, received, SRNOS_NO_WAIT);
}
```

运行期使用保存的通道，不能再次调用 GetChannel。Write 成功后若需要确认线路发完，再调用 Flush 并检查结果；应用不要忙等。协议绑定参考 [PrototypeBindings](../../../Examples/PrototypeBindings/README.md)。
