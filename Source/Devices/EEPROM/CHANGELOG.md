# Changelog

## Unreleased

- 2026-09-29：补充 Storage 与原始驱动 timeout 区别；文档异步 Poll 示例显式让出 CPU，清理旧 BSP 称呼。驱动及硬件时序未改。


- 新增 Storage 私有整记录租约及管理区原始写保护；运行期同 owner 推进/取消合同保持，维护模式仅启动配置可选。原 EEPROM 和实际 Storage+驱动 ASan/UBSan 主机模型通过；本轮未烧录。
- Parameters 转到 FT24C128A 后删除旧 24C02 源码及公共头，不自动迁移旧保存内容。 · 2026-09-28

- 配合 I2C 零等待查询合同，24C02 阻塞 Read/Write 在提交前拒绝 NO_WAIT/非法有限值，避免返回后仍有 RX 在途；支持 SDK FOREVER。实际包装层与 I2C 分发器联合主机回归通过，FT24C128A 状态机及硬件协议未改，未板测。

- 2026-09-28：旧 24C02 源码接口改名为 `SRNOS_EEPROM24C02*` / `SRNOS_EEPROM24C02Blocking*`，相关公共头为 EEPROM24C02.h、EEPROM24C02Blocking.h，无兼容宏或 HAL/RTOS 类型。它们仅服务尚未迁移的上层存储，不代表支持当前 FT24C128A；父项目使用 `SirinEmbedOS/Devices/EEPROM.h` 中的 `SRNOS_EEPROM*`。存储格式和默认禁用策略未因改名改变。 本轮主机/构建范围见 `Docs/Validation/SRNOSBoundary-20260928/README.md`；未重新烧录。

- 增加固定 FT24C128A 的 `SRNOS_EEPROM*` 公共接口：16 KiB、16-bit 内部地址、64 B 分页、分段读取，支持阻塞及任务推进的非阻塞操作。
- 两种入口共享状态机；写周期等待后逐页读回验证，提供超时、同器件互斥和取消，静态 RX 隔离迟到完成；不创建线程、不分配动态内存。
- I2C 取消使用设备所有权检查，避免恢复已经转交其他传感器的总线事务。
- 新增主机测试和 EEPROMBringUp 备份/测试/恢复流程。参数、HostLink 存储和 INS 持久化尚未迁移，旧驱动仅保留未迁移上层依赖。验证证据见模块 README。
- 配合 I2C4 调度：读、页内写及校验均最多 32 B，保留 64 B 页边界；驱动 staging 缩为 32 B，底层 TIMEOUT/CANCELLED 映射为对应器件结果。
- 完成 400 kHz Fast-mode 双 API 整片及边界板测，21 项通过；整片写含校验约 5.12 秒、读约 1.03 秒，同总线传感器持续 READY。本次驱动策略不变，按用户要求保留最终测试内容。
