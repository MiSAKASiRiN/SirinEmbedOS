# 业务参数

父项目声明两类数据：`SRNOS::Parameters::Persistent<T>` 用于允许实时修改并显式持久化的业务参数，`SRNOS::Telemetry::Value<T>` 用于只读高频遥测。两类注册表分离；遥测值不进入 EEPROM，也不能通过 CONFIG WRITE 修改。公共头不需要 FreeRTOS、CMSIS 或 HAL。

## 声明与使用

```cpp
#include <SirinEmbedOS/Services/Parameters/Parameters.hpp>
#include <SirinEmbedOS/Kernel/System.h>

SRNOS::Parameters::Persistent<float> kp(0x1001, "speed.kp", 1.0f, 0.0f, 30.0f);

extern "C" void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config) {
    config->parameter_application_id = 0x50494431; // 本应用的固定身份
    config->parameter_compatibility_version = 1;
}
```

`Persistent` 支持 float、int32_t、uint32_t、bool，必须给出非零稳定 ID、名称、默认值和上下界。启动期复制名称及声明，冻结后禁止新增。重复 ID/名称、无效范围、非有限浮点或容量不足均返回错误；C++ 用 `RegistrationStatus()` 检查，启动冻结也拒绝此前登记的定义错误。默认容量 32 项，可编译期覆盖至 255 项并核对 EEPROM/内存预算。稳定 ID 不随注册顺序变化；修改业务含义时换 ID，不能复用旧 ID。

C 项目使用 `SRNOS_PersistentParameterDefinition`、`SRNOS_ParametersRegisterPersistent()`，通过 `SRNOS_ParameterValue` 的 f32/i32/u32/boolean 对应成员声明默认值及范围。SDK 持有当前值，`SRNOS_ParametersGet/Set` 和 C++ `Get/Set` 只操作副本，不公开控制器对象指针。应用在控制周期边界调用一致 `Snapshot` 取得一组参数，再应用到业务算法。数值读写限任务上下文，批量写入先全部校验，再一次提交；重复 index 也拒绝。快照二进制是 little endian 的 float32/int32/uint32 或 0/1 单字节 bool。

该类型本身表示“允许主机修改并保存的业务数值”，没有旧 `Param`/访问权限兼容层。CAN 模式、UART 绑定、IMU 硬件配置、线程预算和服务开关不能注册为参数。系统不能从一个任意数字推断其业务含义，父项目必须遵守声明边界。只读编码器等观测值使用独立 [Telemetry](../Telemetry/README.md)。

## RAM 修改、保存和加载

- WRITE、`Set`、`ResetDefaults` 只改变 RAM；相同值不会增加 generation，不自动写 EEPROM。
- `Save` 将当前值冻结成固定快照，锁外编码并调用 Storage。保存期间仍允许 WRITE；成功报告 `saved_generation`，新的 RAM 修改保持 dirty。已经确认保存且内容未变时不再执行存储写入。
- `Load` 先流式读取到暂存值，只有整条 Storage CRC、版本和记录布局通过才提交。期间 generation 改变时返回 CONFLICT，不能覆盖并发 WRITE。运行期加载失败保留当前 RAM，标记保存状态不可确认。
- 启动时在父项目线程放行前加载；坏 CRC、空白、应用身份/兼容版本不符或超时使用声明默认值。相同应用/版本内按稳定 ID 加载：未知 ID 忽略，缺失、类型改变或越界项使用该项默认值，并分别计数。
- `GetState` 返回当前/已保存 generation、Storage revision、dirty、保存有效性、忙状态和最近结果。默认回退不自动覆盖 EEPROM。新代 generation 回绕时撤销“已保存”判断，防止误判相等。

无参数时允许 application_id=0；有参数时必须非零，兼容版本也必须非零。`LoadWithTimeout/SaveWithTimeout` 的 0 分别选 2000/5000 ms 默认总期限，有限值不超过 INT32_MAX。调用者会协作阻塞，不得放入实时控制 step；SirinLink SAVE/LOAD 在 HostCmd 执行，HostComm、INS 不等存储。多次 Parameters 存储互斥；与身份/校准存储竞争由 Storage 返回 BUSY。父项目无需自己创建存储线程。

## 格式与资源

Storage 的 PARAMETERS 类别（4）独占业务分区，owner 为 application_id，schema 为 compatibility_version，单份记录不保存上一版。中断写入可能令当前记录失效；启动回退默认是预期合同。参见 [Storage](../Storage/README.md)。不读取旧 24C02 镜像，不自动迁移旧内容。

PARAMETERS payload：8 B 头（LE32 `0x31505253`、LE16 格式 1、LE16 条数），随后按稳定 ID 严格递增的 12 B 记录：LE32 ID、u8 类型、u8 长度、2 B 零保留、4 B little endian 值；bool 仅首字节有效，后三字节为零。禁止重复 ID，字节流不按 C++ 对象布局存储。完整头/载荷/提交校验由 Storage CRC32 覆盖。

默认固定槽表保存 32 组元数据和四份 4 B 数值（当前/默认/上下界），SAVE 工作缓冲为 `8+12×32=392 B`；LOAD 仅暂存 32 个值和 12 B 片段，允许旧记录超过当前注册数量，不分配 8 KiB 镜像。值保护区只做有限复制/提交，编码、遍历和 EEPROM I/O 在保护区外；没有独立 worker 或运行期动态分配。

## 关联接口

`SRNOS_ParametersDumpDebugPort()` 在调度运行或锁定期间，从任务上下文把 `name=value` 逐行交给 SDK Logging，使用 INFO 等级、`Parameters` 来源及正常日志前缀。非空表在没有启用后端或 INFO 被全部过滤时返回 DISABLED；队列/消息池拒绝返回 NO_SPACE，完整行超过日志正文容量返回 BUFFER_TOO_SMALL。首个失败立即结束，已经接纳的前缀不回滚。OK 只表示全部逐行接纳，日志的丢旧和后端丢弃策略仍适用；需要自行管理完整导出时使用 `SRNOS_ParametersDumpRegistry()` 回调。

掉电恢复、EEPROM 寿命和板端时序需要在实际应用条件下验证。

跨模块用法和完整磁盘布局见 [EEPROM 设计](../../../Docs/Storage.md)；完整 SCHEMA/READ/WRITE/SAVE/LOAD 字段见 [参数协议](SirinLink/README.md)。
