# 参数 SirinLink 协议

SDK 在启动注册并冻结 CONFIG 服务。SCHEMA/READ/WRITE 和状态查询在 HostComm 执行；SAVE/LOAD 由已有 HostCmd 延后执行，不能在 HostComm、INS 或中断中写 EEPROM。父项目使用 Persistent 注册业务数值即可，不需要重复注册路由或包含 RTOS/HAL 头。

只有 Persistent 业务注册表允许 WRITE/SAVE；Telemetry 注册表不被这些命令访问，CAN/UART/IMU/服务配置不进入注册表。CONFIG 的 `0x01` SCHEMA、`0x02` READ、`0x03` WRITE、`0x04` SAVE、`0x05` LOAD 保留既有编码与消息号。WRITE 只改 RAM；SAVE 捕获执行时的 RAM 版本，工作期间可以继续 WRITE；LOAD 遇到并发 WRITE 返回 CONFLICT。会话重试、活动请求去重与断连处理继续由 SirinLink 负责。

以下响应描述均不含 SirinLink 添加的 2 B 状态码，所有多字节整数采用 little endian。

| 消息 | 请求 | 响应 |
|---|---|---|
| `0x01 SCHEMA` | u16 cursor、u8 max_count（非零） | u32 layout_hash、u16 total/next、u8 count；每项 u16 index、u8 type/registered/value_size/name_len、名称字节、default/min/max 三个值 |
| `0x02 READ` | u8 count、u16 index[count] | u32 layout_hash、u8 count；每项 u16 index、u8 type/size、value。整批值来自一次一致快照 |
| `0x03 WRITE` | u32 layout_hash、u8 count；每项 u16 index、u8 type/size、value | 校验进入批量提交后为 8 B：u32 当前 layout_hash、u8 count/failed_item/ParametersStatus/reserved=0。failed_item=0xFF 表示无具体失败项；早期长度/hash/类型错误可无正文 |
| `0x04 SAVE` | 空 | 延后完成后 7 B：u32 layout_hash、u16 count、u8 ParametersStatus |
| `0x05 LOAD` | 空 | 与 SAVE 相同；失败保留当前 RAM |
| `0x06 STATE` | 空 | 固定 32 B：u8 version=1，u8 flags，u16 compatibility_version，u32 application_id、generation、saved_generation、storage_revision，u16 last_load、last_save、loaded、defaulted、ignored、count |
| `0x07 IDENTITY` | 空 | 固定 28 B：u8 version=1，u8 valid，u16 StorageStatus，u32 revision，20 B 数字序列号；无效时序列号全部为零字节 |
| `0x08 STABLE_IDS` | u16 cursor、u8 max_count（非零） | u32 layout_hash，u16 total、next，u8 count，随后每项 u16 index、u32 stable_id；结束 next=0xFFFF，否则设 MORE |

SCHEMA 的字符串不含 NUL；default/min/max 按 value_size 编码。分页 next=0xFFFF 表示结束，否则外层 flags 设置 MORE，主机用 next 继续请求。registered 是当前已登记标志，不是可切换的访问权限；该目录只包含 Persistent 业务参数。

类型号 1=float32、2=int32、3=uint32，均 4 B；4=bool，1 B 且只能为 0/1，数值全部显式小端。READ/WRITE count 必须非零且不超过实际配置上限；WIRE index 是当前目录顺序，不能替代 0x08 查询的 stable_id。WRITE 必须携带最新 layout_hash，长度/type/size/范围/重复项有任何错误则不提交整批。SCHEMA/READ 的最大响应还受帧容量限制，主机应分批请求。

STATE flags 的 bit0 为 dirty、bit1 为 saved_valid、bit2 为 Parameters 存储忙；last_load/last_save 为 `SRNOS_ParametersStatus` 数值。STATE 不做 EEPROM I/O。IDENTITY 仅返回身份缓存（由启动加载或本地身份操作刷新）且只读，不提供主机序列号写入命令。STABLE_IDS 补充旧 SCHEMA 中没有的稳定身份，index 仍仅对当前固件目录有效。

SAVE/LOAD 的回复仍为 layout_hash/count/ParametersStatus；BUSY、CONFLICT、存储错误映射到对应 SirinLink 状态。关闭启动配置 `sirin_link_storage_enabled` 时 SAVE/LOAD 返回 UNSUPPORTED_MESSAGE，RAM WRITE 和只读查询仍可使用。注册表与权限冻结，不接受任意地址或系统资源写入。

EEPROM 格式和落盘合同见 [Parameters](../README.md) 与 [Storage](../../Storage/README.md)。

## 交互顺序

1. 打开 CDC1 并 HELLO；分页 SCHEMA 取得当前 index/type/默认值/范围，需要持久化身份映射时再查 STABLE_IDS。重新连接或固件变化后重新获取目录。
2. READ 获取当前值，WRITE 携带当前 layout_hash 原子修改一批 RAM 值。SDK 不直接修改控制器成员，父项目在自己的周期边界读取一致快照应用。
3. 用户明确保存时发 SAVE，等待最终 OK 后查询 STATE。保存期间发生新的 WRITE 时，saved_generation 仍是保存快照的代次，dirty 可继续为真。
4. 显式 LOAD 重新读取 EEPROM；坏记录、总期限或并发 WRITE 使 LOAD 失败时不覆盖 RAM。恢复默认为本地 `SRNOS_ParametersResetDefaults()`，本协议没有另行分配“恢复默认”消息号。

参数结果码：0 OK、1 ERROR、2 INVALID_PARAM、3 NOT_FOUND、4 DUPLICATE、5 NO_SPACE、6 TYPE_MISMATCH、7 OUT_OF_RANGE、8 BUFFER_TOO_SMALL、9 PARSE_ERROR、10 EEPROM_ERROR、11 EEPROM_BUSY、12 BAD_DATA、13 LAYOUT_MISMATCH、14 CRC_ERROR、15 NOT_INITIALIZED、16 LOCKED、17 READ_ONLY、18 CONFLICT、19 TIMEOUT、20 DISABLED、21 CONTEXT_ERROR。它与外层 SirinLink status 及 IDENTITY 的 StorageStatus 不同。

CONFIG 0x09/0x0A 属于独立 IMU 静态校准处理器，不是业务参数命令；参数 SAVE 不会触发校准或改 PCB 序列号。完整 [消息总表与 COBS 帧](../../../../Docs/SirinLink.md)、[EEPROM 数据及启动流程](../../../../Docs/Storage.md)。

## 请求正文示例

以下仅为 CONFIG 请求正文；外层仍需按 [SirinLink](../../../../Docs/SirinLink.md) 添加消息种类、序号、长度、CRC 并 COBS 封帧：

```text
SCHEMA (message 0x01): 00 00 04   # cursor=0，请求至多4项
READ   (message 0x02): 01 00 00   # count=1，读取当前目录index=0
STATE  (message 0x06): <空正文>   # 查询dirty和当前/已保存代次
```

只有 SCHEMA 证明 index=0 存在后才发送上述 READ。序号由主机为会话分配，读取响应时检查外层状态码，再按返回类型解码；不能把示例正文原样写到串口。WRITE 的 layout_hash 和字段类型必须来自本次目录查询，不能复制其他固件的固定字节串。
