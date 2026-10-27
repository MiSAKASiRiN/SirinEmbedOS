# CHANGELOG

## Unreleased

- 2026-10-24：新增私有 W25Q128 1-1-1 驱动与固定 OSPI1 适配，提供 JEDEC、范围/保护检查、WEL/WIP 有限等待、页编程/4 KiB 擦除、完整回读校验与错误后停写。没有连接硬件或修改 CubeMX 生成文件。
- 2026-10-24：核对 Winbond 官方 W25Q128JV Rev F 和 ST OSPI FAQ；实际 HAL `DeviceSize` 保持 24，运行期显式应用 ≤20 MHz、4-cycle CS high 与单线 SDR profile，避免 READY 状态重新 `HAL_OSPI_Init` 不生效。
- 2026-10-24：33 个 fake transport 场景与 13 个 fake HAL 场景通过，开启 ASan/UBSan。测试放在 SDK Tests 树，在主机临时目录生成程序，不访问设备。
- 2026-10-24：首次板测初始化得到 `IO_ERROR`，寄存器仍保留 CubeMX 的 /3 分频、1-cycle CS high 与 5000 ms HAL 超时，未发 NOR 命令。根因是当前 HAL 的 `HAL_RCCEx_GetPeriphCLKFreq()` 未实现 OSPI 分支而返回 0；适配改为先检查固定 D1HCLK mux，再调用 `HAL_RCC_GetHCLKFreq()`。fake HAL 删除原先错误模拟为受支持的通用查询，增加 wrong_mux 无配置改动/无命令测试，保留零频率和分频向上取整回归。
- 2026-10-24：首次 JEDEC 前增加 SR1 的 500 ms 有界就绪等待，避免 MCU 单独复位时把仍在擦写、忽略 `9Fh` 的 NOR 误判为不支持。增加 BUSY 阶段忽略 JEDEC 的恢复、持续 BUSY 与全 FF 状态超时用例；未知身份不发写命令。
- 2026-10-24：使用父项目 ST Arm Clang 的 Cortex-M7/硬浮点/`-Oz` 参数分别编译 `nor.c`、`nor_board.c` 成功；追加 `-Wall -Wextra -Werror -Wno-unknown-attributes`，最后一项仅针对既有 CMSIS `optimize` 属性与 Clang 的兼容警告。单函数静态栈报告：Program 280 B、EraseSector 288 B、板级 Transfer 120 B；这些数值不包括被调函数、HAL、TLS 或中断栈。
- 2026-10-24：修复固件独立 Blackbox 板测初始化与 JEDEC 识别通过，读得 `EF4018`。20×224 B、序号 1..20 跨扇区编程/完整回读通过，committed=20、合计 265 ms；MCU 复位后只读相同 20 条通过，耗时 105 ms、本次启动 committed=0，两轮读写错误均为 0。这验证了本板有限次命令/数据访问，不等同于完整器件后缀、电气时序或保护矩阵验收。
- 待验证：供电掉电、满环回绕、脏扇区擦除、100 Hz 长期负载、控制并发与线程栈水位、寿命；完整订货后缀、线路波形和状态保护矩阵仍需对应资料/测量确认。
