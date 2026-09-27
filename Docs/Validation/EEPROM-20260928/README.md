# FT24C128A 器件验证 · 2026-09-28

本轮使用源码检查、FMD DS24C128-A2、主机模型、ST Arm Clang 构建和唯一 J-Link/GDB 会话。用户确认实板 EEPROM 已换 FT24C128A，并明确允许接管会话、加载专用固件、先备份后测试恢复；未修改在线 EDA 或重新生成 CubeMX。

## 结果

- `EEPROMBringUp` Debug 板端完成 20 项检查、0 失败，测试及恢复窗口 22141 ms，异步推进 7557 次。两种入口都覆盖 16 KiB 全片；包含跨 64 B 页、`0xFF/0x100`、`0x1FF3`、`0x3FFF` 末字节及非法范围拒绝。
- 流程为阻塞全片写/异步全片读、异步全片写/阻塞全片读、边界验证、原始备份全片写回及再读。驱动每页写后等待 6 ms 并比对本页数据。最终 `stage=6`、`restored=true`、保存/恢复 FNV-1a 均为 `1212669381`。
- 主机独立比较 `eeprom-before.bin` 与 `eeprom-restored.bin`：16384 字节完全相同，SHA-256 均为 `0fbba07a833d4dcfc7024eaf313661a0ba8f80a05c6d29b8801c612e10e60dee`。初始内容全为 `0xFF`。
- I2C 观测 `xfer_error=0`、`xfer_timeout=0`、`recover_count=0`。本轮没有总线故障/断线/写保护硬件注入，没有掉电写入或保持寿命验收。

## 已知共享访问限制

环境传感器 `sensors.c` 的 i2c_read/write 把获取总线返回 BUSY 直接变成失败，worker 再置 OFFLINE。这是当前即时获取总线与传感器失败策略的组合，不是 EEPROM ACK/数据读写故障。测试前 LIS/SPA 为 READY；22 秒整片密集访问期间错误计数各从 2 增至 24，测试末快照为 OFFLINE。停止 EEPROM 访问后，两者恢复 READY：LIS samples=5847、SPA samples=1027，recoveries 各为 1，错误计数未继续增加。

原始快照在 `gdb.log`。本轮按器件优先范围保留传感器策略及参数/INS 存储，后续接入持久化前需处理 BUSY 重试/仲裁；不能将本次结果解释为共享服务实时性验收。部分暂停会扰动传感器时序，实际测试窗口未暂停。

## 离线验证与资源

- 20 组主机套件通过。EEPROM 测试启用 ASan/UBSan，覆盖真实驱动的全范围、页/地址边界、buffer 合同、所有权、模拟 NACK/写保护/超时/取消/隔离、tick 回绕和调用上下文；I2C 套件使用实际 BSP 验证设备级取消不干扰其他设备。
- EEPROMBringUp Debug/Release 编译链接通过；Debug 板上向量表及 `.text` 与 ELF 比较匹配。Debug ELF SHA-256：`f32a2618671ab636de22fa0da6761790029c28731aca41278cbddfd8f423f920`。
- Debug：Flash 252048 B，D1 316640 B / 320 KiB，D2 10240 B / 32 KiB，D3 512 B / 16 KiB；DTCM 128 KiB 为链接预留总量，非运行实际耗尽。板端剩余/最低 heap 均为 96056 B。
- BSP 的 RX/TX staging 仍在 `0x38000000/0x38000100`。仅验证例程增加 D2 非缓存命令/状态和测试数据缓冲；驱动无动态分配、无新线程，未修改链接脚本或 MPU。
- 构建保留上游 CMSIS 的已有 `optimize` attribute 警告；本轮修改文件差异格式检查通过。仓库另有用户既存的 `Board/Glue/usb_device_board.c:163` 行尾空格，本轮未处理。

## 调试过程与恢复

接管前唯一 J-Link V8.96 服务器使用探针 `10114514`、STM32H723VG、SWD 4000 kHz、端口 2331。旧 GDB 的 `/tmp/srnos-ins-20260928.elf` 与实板向量/代码不匹配，因此未用该 ELF 恢复。先保存完整 Flash `[0x08000000,0x08100000)` 到 `firmware-before.bin`，SHA-256 为 `086a7bf9cc3229e189ecd1c386b14d276e188f364b288f71872f35307b0fd74f`。

第一版验证例程把命令放在缓存 RAM，GDB 写入未被任务看到，始终 stage=1，未开始任何 EEPROM 写入。初始 SWD dump 也存在缓存可见性问题；保留的 `initial-backup-stale-cache.bin`、`initial-work-test-not-started.bin` 不是有效备份/恢复证据。修正例程为命令/状态使用现有非缓存 Debug Watch，备份/恢复数据在发布前 clean D-cache 后，重新备份，核对 CPU/主机 FNV 一致，才执行写入测试。此前两个静态 16 KiB 数组曾导致验证固件 D1 链接溢出，改为 16 KiB 静态备份 + 16 KiB 测试专用 heap 工作区，不改变生产内存配置。

EEPROM 恢复确认后，将原始 Flash 备份封装为加载镜像并回写；回写后整个 1 MiB 与备份比较匹配，再复位运行。最后一次取证 PC=`0x080374AC`、SP=`0x24028700`，CFSR/HFSR 均为 0，随后继续运行。没有新增或遗留断点。GDB/J-Link 会话保留，MCU 运行原始板上固件；当前 GDB 使用原始 Flash 备份镜像，无原始调试类型符号。

文件：`summary.json` 汇总哈希/结果，`gdb.log` 保存操作与现场输出，`host-tests.log`、`build-debug.log`、`build-release.log` 保存离线验证。所有备份均留在本目录，无提交或推送。
