# Blackbox

板载 W25Q128 的有界二进制循环记录服务。公共入口为
[`Services/Blackbox.h`](../../../Include/SirinEmbedOS/Services/Blackbox.h)，由 SDK
`SRNOS_Blackbox` 专用线程独占 NOR 初始化、挂载、读取、编程和擦除。父项目不得调用
NOR/OSPI/HAL 或自行创建 Flash 线程。EEPROM 的 Storage 接口和分区不受本服务管理。

## 接入与线程

在已有 `SRNOS_ApplicationConfigure` 中设置：

```c
config->blackbox.enabled = true;
config->blackbox.queue_depth = 32;
```

SDK 默认关闭；配置在启动阶段冻结，深度为 8..64 条。启动时一次分配服务对象与队列，
创建优先级 8、4096 B 栈的 SDK worker；运行期间不分配内存，不使用应用对象池。
32 条队列的保守启动预算在 Cortex-M7 上为 15104 B，实际对象受编译期大小断言约束。
父项目需通过 `SRNOS_SystemHeapBudget` 和 ELF/map 检查预算；这不是 DMA 分配器。

挂载和设备识别在 worker 中执行，不阻塞应用启动。首次使用会扫描整个 12 MiB 分区，
后续启动扫描扇区头和需要恢复的记录；稀疏/损坏区域可能同样需要扫描整个分区，
READY 前 Append 返回 NOT_READY。
扫描每 32 次 I/O 协作等待 1 ms，NOR 忙等待也主动让出 CPU；无长关中断区。
所有公开运行接口只允许调度中的任务调用，拒绝 ISR、PRIMASK/BASEPRI 屏蔽上下文。

## 写入

生产者显式编码自己的载荷，再调用：

```c
SRNOS_BlackboxStatus result = SRNOS_BlackboxAppend(type, timestamp_us, bytes, length);
```

- type 为非零、应用定义的记录格式 ID；payload 为 1..224 B，避免直接持久化 C/C++ struct。
- API 有界复制数据并零等待入队，任何返回后都可复用源缓冲。
- OK 仅表示接受入队；`GetSnapshot().committed` 才表示本次启动中完整写入并读回校验的条数。
- 队列满丢弃新记录并增加 `dropped_full`，不覆盖待写记录、不阻塞控制线程。
- NOR 错误/未知介质使服务进入 FAULT，停止后续 Flash 操作，未写队列计入 `discarded_fault`。
  不因缺少 Flash 或保护位自动停止整个 SDK；不自动解锁、整片格式化或反复重试。
- RAM 中已接受但未提交的记录会在掉电时丢失；记录校验与提交标记保证读者不接受残片，
  不承诺最后一次提交在任意供电中断下仍可恢复。

worker 逐条写入；换扇区时在 worker 内擦除 4 KiB 并校验。**当前没有预擦除或擦除挂起机制**。
32 条队列在 100 Hz 时约覆盖 320 ms；器件最坏擦除时间加回读可能更长，必须观察满队列计数。
容量与入队上限不等于持续写入速率或零丢样保证，需实板验证长时间负载、调度、供电和寿命。

## 读取

读取也完全由同一个 worker 执行。接口保留游标与 ticket，不保留调用者指针：

1. `SRNOS_BlackboxRequestRead(after_sequence, &ticket)` 提交请求，0 表示从最早记录开始。
2. 在后续业务周期调用 `SRNOS_BlackboxPollRead(ticket, &record)`；BUSY 表示尚未完成。
3. OK 返回首条 sequence 大于游标的有效记录，用该 sequence 继续；EMPTY 表示目前到达末尾。
4. 完成结果会释放槽位；可用 `SRNOS_BlackboxCancelRead(ticket)` 放弃尚未消费的请求。

全服务只有一个读请求/结果槽；其他请求返回 BUSY。取消不强行中断已开始的总线操作，
旧结果通过 ticket 隔离，不能覆盖新请求。worker 在最多处理一条写入后处理一次读请求。
单次读在稀疏或损坏区域可能扫描多个扇区，完成前会延迟后续写入；取消只丢弃结果，
不终止已经进行的完整扫描。持续记录时读回历史也应观察 `dropped_full`。
公开接口不等待 Flash；不要在应用中忙轮询。示意片段（放入正常周期任务，并自行处理记录）：

```c
static uint32_t ticket;
static uint64_t cursor;
if (!ticket) {
    (void)SRNOS_BlackboxRequestRead(cursor, &ticket);
} else {
    SRNOS_BlackboxRecord record;
    SRNOS_BlackboxStatus status = SRNOS_BlackboxPollRead(ticket, &record);
    if (status == SRNOS_BLACKBOX_OK) {
        cursor = record.sequence;
        /* Consume/copy record here in the application task. */
    }
    if (status != SRNOS_BLACKBOX_BUSY) ticket = 0;
}
```

环形覆盖、损坏记录、断电残片会形成序号间隙，读取跳过这些位置。读取不会锁定历史快照，
读取期间仍可能覆盖最老数据。序号是跨启动持久顺序；timestamp_us 是生产者提供的时间。
需要区分重启或 UTC 有效性时，应在载荷中带 boot ID、schema 和时间有效位。
未提供网络导出协议、锁定事故快照、清空/格式化命令或 DFU。

## 磁盘布局与恢复

只访问固定分区 `0x100000..0xCFFFFF`（12 MiB）：

| 范围/结构 | 用途 |
|---|---|
| `0x100000..0x100FFF` | 不参与回环擦除的 volume header 扇区 |
| `0x101000..0xCFFFFF` | 3071 个循环扇区，每个 4096 B |
| 每扇区第一页 | 版本、几何、generation/index、CRC、提交标记 |
| 每扇区后 15 页 | 每页一条 256 B 记录 |

最大 46065 条记录，100 Hz 且没有丢弃时约 **7 分 41 秒**；换扇区时按扇区整体回收，
可见历史会暂时少最多一个扇区。这个数已扣 volume 和扇区头，不再使用未扣开销的 8 分 12 秒估算。
`0..0x0FFFFF` 与 `0xD00000..0xFFFFFF` 保持预留，不写事件区、校准区或固件槽。

固定记录采用小端：magic/version 4 B，sequence 8 B，timestamp_us 8 B，type 2 B，
length 2 B，CRC32 4 B，commit 4 B，payload 224 B。不足部分填 FF。
先写 body 并读回，再单独写 commit 并再次读回；CRC 覆盖头和固定 payload，排除 CRC/commit 自身。
序号为 `generation * 15 + slot + 1`，溢出后拒绝继续写。

mount 只读。首次 Append 仅在整个分区空白，或精确匹配本布局的可续写 volume 残片且
其余分区完全空白时完成 volume；未知格式/非空陌生数据返回错误，不自动擦除。
有效 volume 建立后，环区属于该布局；重启扫描有效 generation，消耗已部分编程的槽，
跳过无效记录。换扇区不会擦除当前最高 generation 扇区；被中断的旧扇区擦除可在后续回环重做。
若无效扇区头下面的完整记录证明存在更新的一代，服务返回 CORRUPT 并保留介质，
禁止回退 generation 后复用序号。此类已提交头损坏需要维护处理，不自动擦除继续记录。

板级实现、JEDEC 白名单和芯片时序见 [NOR](../../Devices/NOR/README.md)。
