# 单份 EEPROM 持久化

Storage 管理固定 FT24C128A 的记录格式和操作租约，没有独立线程或动态内存分配。启动期冻结 `SRNOS_StorageConfig`；默认启用管理、禁止工厂身份写入。父项目通过 Parameters、INS 校准和身份能力访问记录，不依赖私有地址或 RTOS/HAL 类型。

## 分区与格式

完整分区、记录头/提交页、身份/校准/参数正文、启动流程和异常处理统一见 [EEPROM 存储设计](../../../Docs/Storage.md)。Storage 只保存单份记录，CRC 检测损坏，不保证掉电保留上一版；原始写保护范围为 `0x0000..0x2FFF`。本页说明服务接口与任务上下文，原始器件推进及每段超时见 [EEPROM 驱动](../../Devices/EEPROM/README.md)。

## 调用与并发

私有 `storage_internal.h` 向 Parameters/INS 提供有界 ReadRecord/VisitRecord/WriteRecord。VisitRecord 每次在保护区外交付最多 64 B 临时块，不需要全记录镜像；回调不能阻塞、I/O 或保留块指针，返回 false 中止为 CORRUPT。所有块均为暂存结果，只有整个调用最终 OK 才能发布，末尾 CRC 失败时必须丢弃全部暂存值。正文由调用方提供，写入期间须保持不可变快照；返回后无指针留存。失败读取可能已复制部分正文，但 length/revision 输出为零，调用者必须忽略正文。

公共身份接口只处理 20 位 ASCII 数字，保留前导零，读快照时提供末尾 NUL。未配置或坏记录返回 invalid 和空字符串，不伪造全零编号。写身份只能由启动时显式启用 `factory_identity_write` 的本地调用执行，普通 HostComm 参数命令无身份写入路由。

除 DefaultConfig/Prepare 外，接口限调度后的任务上下文并要求 IRQ 开启。`timeout_ms=0` 使用读 2000 ms / 写 5000 ms，其余为 `1..INT32_MAX`，覆盖整个操作而非逐页重置。每次 Poll 等待一个 tick，写周期释放 I2C；取消由发起任务执行，I2C 停止确认可能增加现有恢复的有界开销。不得在实时控制回调中执行同步保存。

整个记录操作持有 EEPROM 私有租约，其他存储调用及原始 I/O 立即 BUSY；同一任务也不能用原始接口绕过租约。在无租约时，启用管理仍禁止原始写覆盖 `0x0000..0x2FFF`。工厂全片测试须在启动配置中设置 `enabled=false`，不能运行时解锁。Storage 固定工作区为 3×64 B；保存快照由所属服务单独预算。无新 DMA 缓冲、线程、栈或链接段。

## 工厂身份配置示例

以下代码仅用于明确选择的工厂应用；生产应用保留默认关闭。工厂开关在启动配置冻结后不能修改，写入在调度后的低优先级任务中显式调用：

```c
#include "SirinEmbedOS/Kernel/System.h"
#include "SirinEmbedOS/Services/Storage.h"
void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config) {
    config->storage.factory_identity_write = true;
}
/* 在工厂命令确认完整20位数字后调用，保留前导零。 */
SRNOS_StorageStatus factory_assign_serial(const char serial[20]) {
    return SRNOS_StorageWriteIdentity(serial, 5000);
}
```

普通应用只调用 `SRNOS_StorageGetIdentity(&copy)`，先检查返回值和 `copy.valid`，再使用 `copy.serial`。注册业务参数或执行 CONFIG WRITE/SAVE 都不能开启工厂写入或修改身份。

## 验证

`bash Tests/Services/Storage/run_host_tests.sh` 使用实际 Storage + FT24C128A 驱动和 I2C 器件模型，ASan/UBSan 覆盖每个写入字节断电后的重新加载、头/正文/提交逐字节损坏、8064 B 上限/流式访问、末尾损坏与 visitor 中止、分区隔离、CRC 已知向量、管理保护、不同任务竞争、无变化不写、身份与工厂权限、总期限、取消和时钟回绕。EEPROM 与 HostLink Codec 提供独立回归；主机模型不能代替真实断电、并发总线与保存延迟测量。