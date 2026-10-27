# 固定 I2C4 通路

`SRNOS_I2C` 管理当前板 I2C4（400 kHz Fast mode，实际频率需按硬件实测确认），PB7 SDA/PB8 SCL 的 HAL 初始化归 Board。公开头为 `SirinEmbedOS/IO/I2C/I2C.h`，仅依赖 SDK/标准类型，父项目不需要包含 HAL、FreeRTOS 或 CMSIS。设备保存 7-bit 地址，仅在 HAL 调用边界左移一次。支持 `SRNOS_I2C_ADDRESS_8BIT` / `SRNOS_I2C_ADDRESS_16BIT` memory address。

## 调度

唯一 I2C4 分发线程为 High6 / 3 KiB，提交方只入队，HAL 启动、DMA staging、完成复制、回调和恢复均由该线程负责。中断只发布结果并唤醒线程。总线不在运行期分配槽位或创建额外线程。

四个静态槽中，LIS `0x1E`、SPA `0x76` 各保留一个，两槽留给 EEPROM 等后台设备。同一物理地址最多一个未消费请求，不能通过多个 handle 绕过。类内 FIFO，优先周期类；连续启动四个周期事务后，若后台有排队事务则放行一个，避免存储饥饿。正在传输的事务不可抢占。

单事务限定 1..64 B。长操作由了解协议的设备驱动分段，不能在总线层任意拆寄存器操作。FT24C128A 读、写及写后校验每段最多 32 B，写入不跨 64 B 页边界；器件内部写周期等待不占总线。总线忙时，另一个设备仍可入队。BUSY 表示同地址已有请求或后台槽满，需要调用者退让重试。

周期/后台排队期限分别 20/100 ms，活动事务期限 10 ms。LIS/SPA 使用 30 ms 完成等待覆盖排队与传输。调度保证有界竞争，不保证每个样本都恰好 10 ms；历史连续性实测见 [CHANGELOG](CHANGELOG.md)。

提交先在短临界区保留槽位（`RESERVED`），在保护范围外复制 TX，再发布排队状态并通知分发者。复制期间槽位及设备仍归该请求，其他相同地址提交返回 BUSY；取消等待提交者完成发布及分发者确认，不能提前让调用者释放缓冲区。`SRNOS_I2CIsBusBusy` 同样包括保留阶段。

## 完成与所有权

- 所有提交、等待、取消和探测均为调度器运行后的任务接口；禁止 ISR、关闭中断或锁住调度器时调用。设备 handle 由其调用者串行管理，不能由多任务无锁交错提交与取结果。
- TX 入队即复制，RX 内存保留至完成通知或取消确认。所有公开传输都经过复制队列及 64 B 限制；没有 Direct 或原始 bus acquire/release 公共入口。
- 使用 256 B TX + 256 B RX D3 非缓存 BDMA 缓冲；HAL 永远不直接访问用户 RAM。四个队列 TX 副本位于普通 RAM。
- `pending` 包含提交复制、排队及传输；`SRNOS_I2CWaitComplete` 使用每槽完成信号量。公开头直接提供 `SRNOS_NO_WAIT` / `SRNOS_WAIT_FOREVER`，有限等待为 `1..INT32_MAX` 毫秒。NO_WAIT 只查询，未完成或回调仍占用时立即返回 BUSY，不取消、不安装等待者；BUSY 时调用者仍须保留缓冲区。正值等待从调用开始计时并包含余下排队时间；超时取消对应事务并等待确认后才返回 TIMEOUT。FOREVER 取消调用者期限，但保留 20/100 ms 排队和 10 ms 活动保护。已经完成则返回设备保留结果。
- 完成回调在分发线程执行，必须短小；可提交后续异步事务，不得等待、延时、打印或调用同步探测/取消/恢复。回调和阻塞等待混用时，不在该回调复用设备。
- 取消保留或排队事务不影响活动设备。活动取消经 HAL abort 和 HAL/BDMA 静止确认；失败隔离总线，后续请求失败，需重启处理。总线恢复只取消入口观察到的活动请求，保留提交中和排队的工作。取消不是 EEPROM 写入回滚。正常传输确认静止并清理挂起 IRQ 后才复用 staging。
- `SRNOS_I2CDeviceReady` 也排队，每个试探只执行一次 1 ms HAL probe；多次尝试受调用者总期限约束，不在分发线程循环等待 EEPROM 写周期。NO_WAIT 无提交地返回 TIMEOUT；FOREVER 取消总期限，但保留有限 trials 和单次硬件保护。若提交期间耗尽有限预算，会显式取消并确认释放，不把零等待查询当作取消。

`SRNOS_I2CSnapshot` 分周期/后台统计接纳、完成、错误、超时、取消、队列满、最长排队与传输耗时，另有排队数、高水位和隔离状态。`cb_queued/cb_queue_overflow` 为零，完成回调由分发线程直接执行。

故障注入属于主机模型，不能替代板端断线、卡 SDA 或掉电实验。

板级时序由 `Board/Support/IO/i2c_timing.h` 与 IOC 维护，启动检查 PLL3R=60 MHz 和滤波设置后应用；总线队列和期限遵循上文合同，EEPROM 使用 32 B 分段。板级接入见 [Support](../../../Board/Support/README.md)。

## 板级边界

板级 I2C4 handle、空闲检查与固定时序位于 Board/Support/IO；保留 400 kHz TIMING 与现有调度。

## 托管线程

`SRNOS_I2C4` 由统一框架调用单轮分发，完成就绪立即处理，其他时候等待通知或最近排队/活动截止点，空闲无限等待；不按 tick 轮询活动事务。检查状态后没有额外清除通知的窗口，完成和提交事件可提前唤醒。异常 abort 确认及协作取消仍可按 tick 等待对方完成，不属于常驻轮询。栈、优先级与所有权见上文。私有等待适配由 Kernel 驱动；普通等待不计入 step，处理期间 I/O/抢占/中断计入墙钟。统计不能替代板端时序验收。详见根文档及 Source/Kernel/README.md。

## 任务侧读取示例

以下片段供一个已初始化、由本任务独占管理的外部设备 handle 使用；`reg` 和 `length` 按器件协议指定（1..64 B）。不要用新 handle 绕过 SDK 对 LIS/SPA/EEPROM 的所有权。

```c
#include "SirinEmbedOS/IO/I2C/I2C.h"
SRNOS_I2CStatus read_register(SRNOS_I2CDevice *device, uint16_t reg,
                               uint8_t *data, size_t length) {
    SRNOS_I2CStatus result = SRNOS_I2CMemRead(device, reg,
        SRNOS_I2C_ADDRESS_8BIT, data, length, 0, 0);
    if (result != SRNOS_I2C_OK) return result;
    return SRNOS_I2CWaitComplete(device, 30);
}
```

设备以 `SRNOS_I2CDeviceInit(&device, SRNOS_I2C_BUS_4, address_7bit)` 绑定，地址不预先左移。上例同步等待在超时后确认取消，返回后调用者才能释放 RX 缓冲；若改成 NO_WAIT 查询，BUSY 时必须继续保留缓冲。历史记录见 [CHANGELOG](CHANGELOG.md)。
