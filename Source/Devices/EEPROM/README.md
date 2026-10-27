# FT24C128A EEPROM

当前 2613 固定器件接口为 `SirinEmbedOS/Devices/EEPROM.h`。绑定 I2C4、7-bit 地址 `0x50`，容量 16 KiB（`0x0000..0x3FFF`）、两字节内部地址、64 字节写页。WP 接地、地址脚为低的板级合同来自 2613 硬件记录及用户换型确认；IOC 的 PB7/PB8 对应 SDA/SCL。

器件依据：[FMD DS24C128-A2](https://www.unikeyic.com/media/datasheet/76/4c/f89a/76/98e7dd082231d5e45d88147cae69f5e5.pdf)，第 1、3、5、6 页。最大内部写周期 5 ms。驱动使用 HAL 毫秒 tick 等待 6 ms，补偿整数 tick 的取整；期间释放共享总线。随后读取本次写入的全部字节进行比对，写保护或数据不一致返回 `SRNOS_EEPROM_VERIFY_ERROR`。没有软件模拟 ACK 成功，也不把 DMA 发送完成当作写入成功。

## 使用

所有操作在调度器运行后的任务上下文调用，IRQ 必须开启；不支持 ISR、临界区、锁住调度器或启动配置回调。固定器件只有一个私有上下文，同一时刻只接受一个操作；调用者不得通过旧驱动或另一个 I2C handle 绕过所有权访问同一 EEPROM。

下列为任务函数内的调用片段；写入使用管理区之外的 `0x3000` 扩展区，执行前确认该区没有需要保留的数据。

```c
#include "SirinEmbedOS/Devices/EEPROM.h"
#include "SirinEmbedOS/Kernel/Thread.h"

uint8_t data[300] = {0};
SRNOS_EEPROMStatus status = SRNOS_EEPROMRead(0x0123, data, sizeof(data), 100);

/* 非阻塞：由接受操作的同一任务定期推进；Start 本身不访问总线。 */
status = SRNOS_EEPROMWriteAsync(0x3000, data, sizeof(data), 100);
if (status == SRNOS_EEPROM_OK) {
    do {
        status = SRNOS_EEPROMPoll();
        /* 此示例协作等待；事件/周期任务也可每轮只调用一次 Poll。 */
        if (status == SRNOS_EEPROM_IN_PROGRESS) (void)SRNOS_Delay(1);
    } while (status == SRNOS_EEPROM_IN_PROGRESS);
}
```

生产事件/周期任务可在自己的 step 中每次调用一次 Poll；示例循环每次等待均让出 CPU，不应去掉 Delay 后忙等。阻塞 Read/Write 复用相同状态机，每次等待让出一个 RTOS tick；非阻塞接口无常驻线程、队列、回调或动态分配。调用方负责持续 Poll 或显式 Cancel。

- 地址与长度单位都是字节。拒绝零长度、空缓冲、超出末地址和长度溢出；读取自动分成最多 32 B，写入每段最多 32 B 且不跨 64 B 页边界，支持整片操作。
- `timeout_ms` 范围 `1..INT32_MAX`，分别用于每次提交重试及接受后排队/传输；I2C 分发者自身另有后台 100 ms 排队和 10 ms 活动期限，不是整个跨页操作的总期限。推进频率会影响总耗时。
- 调用者保持 RX/TX 缓冲有效至终态 Poll 或 Cancel；TX 期间不能修改。RX 出错可能包含已完成的前缀。驱动只将静态 staging 暴露给底层 RX，取消后不会再触碰用户缓冲。
- Poll 返回 `IN_PROGRESS` 或最终结果；新请求在已有操作时返回 `BUSY`。其他任务不能 Poll/Cancel 当前操作。取消和错误不回滚已经写入的页面。
- 正常 Poll 不等待写周期、不执行阻塞 ACK probe；DMA 地址和数据由中断推进。超时/取消经 I2C 与 Board Support 按设备确认归属后恢复，abort 最多等待 5 ms 加上调度开销；失败隔离总线，不伪报成功。因此不是硬实时 API。
- 使用已有 I2C4 BDMA staging，不要求应用增加 DMA 段或额外线程。驱动自身只占一个静态状态及 32 B 中转缓冲。

## 存储管理与维护

身份、IMU 校准和业务参数由 [Storage](../../Services/Storage/README.md) 统一管理。启用管理时原始写入覆盖 `0x0000..0x2FFF` 返回 `SRNOS_EEPROM_READ_ONLY`；未管理的扩展区从 `0x3000` 开始。Storage 的整个记录操作持有私有租约，在分段/等待期间所有原始读写仍返回 BUSY，同一任务也不能绕过。Poll/Cancel 仍只允许操作的发起任务。

全片测试在启动时显式将 Storage 配置为 `enabled=false`，不支持运行时解锁。不支持 24C02 的旧格式自动迁移。

## 验证

不将主机模型等同于板端电气或掉电验证。

## 父项目依赖

父项目只包含 `SirinEmbedOS/Devices/EEPROM.h` 使用原始能力，常规业务通过 Parameters、INS 校准及 Storage 身份接口访问管理记录，不包含私有 EEPROM/RTOS/HAL 头。

业务分区与存储超时对照统一见 [持久化总览](../../../Docs/Storage.md)；原始驱动每段 timeout 与 Storage 整体 deadline 不同。
