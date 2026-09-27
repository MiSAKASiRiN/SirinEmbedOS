# FT24C128A EEPROM

当前 2613 固定器件接口为 `SirinEmbedOS/Devices/EEPROM.h`。绑定 I2C4、7-bit 地址 `0x50`，容量 16 KiB（`0x0000..0x3FFF`）、两字节内部地址、64 字节写页。WP 接地、地址脚为低的板级合同来自 2613 硬件记录及用户换型确认；IOC 的 PB7/PB8 对应 SDA/SCL。

器件依据：[FMD DS24C128-A2](https://www.unikeyic.com/media/datasheet/76/4c/f89a/76/98e7dd082231d5e45d88147cae69f5e5.pdf)，第 1、3、5、6 页。最大内部写周期 5 ms。驱动使用 HAL 毫秒 tick 等待 6 ms，补偿整数 tick 的取整；期间释放共享总线。随后读取本次写入的全部字节进行比对，写保护或数据不一致返回 `SRNOS_EEPROM_VERIFY_ERROR`。没有软件模拟 ACK 成功，也不把 DMA 发送完成当作写入成功。

## 使用

所有操作在调度器运行后的任务上下文调用，IRQ 必须开启；不支持 ISR、临界区、锁住调度器或启动配置回调。固定器件只有一个私有上下文，同一时刻只接受一个操作；调用者不得通过旧驱动或另一个 I2C handle 绕过所有权访问同一 EEPROM。

```c
#include "SirinEmbedOS/Devices/EEPROM.h"

uint8_t data[300] = {0};
SRNOS_EEPROMStatus status = SRNOS_EEPROMRead(0x0123, data, sizeof(data), 100);

/* 非阻塞：由接受操作的同一任务定期推进；Start 本身不访问总线。 */
status = SRNOS_EEPROMWriteAsync(0x0123, data, sizeof(data), 100);
if (status == SRNOS_EEPROM_OK) {
    do {
        status = SRNOS_EEPROMPoll();
        /* IN_PROGRESS 时可继续其他任务工作，下个周期再 Poll。 */
    } while (status == SRNOS_EEPROM_IN_PROGRESS);
}
```

生产任务应在自己的循环中每次调用一次 Poll，避免用上面的示意循环忙等。阻塞 Read/Write 复用相同状态机，每次等待让出一个 RTOS tick；非阻塞接口无常驻线程、队列、回调或动态分配。调用方负责持续 Poll 或显式 Cancel。

- 地址与长度单位都是字节。拒绝零长度、空缓冲、超出末地址和长度溢出；读取自动分成最多 32 B，写入每段最多 32 B 且不跨 64 B 页边界，支持整片操作。
- `timeout_ms` 范围 `1..INT32_MAX`，分别用于每次提交重试及接受后排队/传输；I2C 分发者自身另有后台 100 ms 排队和 10 ms 活动期限，不是整个跨页操作的总期限。推进频率会影响总耗时。
- 调用者保持 RX/TX 缓冲有效至终态 Poll 或 Cancel；TX 期间不能修改。RX 出错可能包含已完成的前缀。驱动只将静态 staging 暴露给底层 RX，取消后不会再触碰用户缓冲。
- Poll 返回 `IN_PROGRESS` 或最终结果；新请求在已有操作时返回 `BUSY`。其他任务不能 Poll/Cancel 当前操作。取消和错误不回滚已经写入的页面。
- 正常 Poll 不等待写周期、不执行阻塞 ACK probe；DMA 地址和数据由中断推进。超时/取消经 BSP 按设备确认归属后恢复，abort 最多等待 5 ms 加上调度开销；失败隔离总线，不伪报成功。因此不是硬实时 API。
- 使用已有 I2C4 BDMA staging，未增加 DMA 段、修改 MPU/链接脚本或 SDK 线程预算。驱动自身只占一个静态状态及 32 B 中转缓冲。

## 与旧上层的边界

`lib_24c02.*` / `lib_24c02_bl.*` 暂保留给尚未迁移的参数模块编译依赖；它们仍是旧器件协议，禁止用于 FT24C128A。新 API 不是旧名字的别名。参数区域、格式、自动加载、HostLink LOAD/SAVE 和 INS 标定持久化本轮均未迁移，默认存储开关继续关闭。

## 验证

`bash Tests/Devices/EEPROM/run_host_tests.sh` 验证实际驱动状态机：16 KiB 全范围、跨页/跨 256 B/末地址、读回校验、并发所有权、NACK、写保护、总线忙、事务超时、取消、隔离、tick 回绕及调用上下文，启用 ASan/UBSan。

板端入口为 [EEPROMBringUp](../../../Examples/EEPROMBringUp/README.md)，先备份并由主机保存后才能显式触发写入；实测范围见 [验证记录](../../../Docs/Validation/EEPROM-20260928/README.md)。不将主机模型等同于板端电气或掉电验证。

此前器件板测发现密集读写触发环境传感器 BUSY 即离线；当前 I2C4 已增加有界分发队列及 LIS/SPA 保留槽，新的并发测试见 [I2C 验证记录](../../../Docs/Validation/I2C-20260928/README.md)。参数/INS 上层存储继续默认关闭。
