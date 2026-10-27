# 私有 W25Q128 NOR 驱动

本模块给 SDK 黑匣子 worker 提供 16 MiB 外置 NOR 的同步、有限等待访问。驱动不公开给父项目，不创建线程，不分配堆，不使用 DMA、自动轮询 IRQ、memory-mapped 模式或长临界区。OSPI1 由这一 worker 独占；参数存储仍使用自己的 EEPROM。

## 接入与合同

生产构建加入 `nor.c` 和 `Board/Support/Devices/nor_board.c`，使用已有 `Source`、`Board/Support` 私有 include 路径。服务只包含 `Devices/NOR/nor_internal.h`。

| 私有函数 | 合同 |
|---|---|
| `SRNOS_NORInitialize(uint32_t *jedec_id)` | worker 在调度器运行后调用；返回实际 24 位 JEDEC ID，未读到时为 0；每次启动只尝试一次，重复调用不能清故障。 |
| `SRNOS_NORRead(address, data, length)` | 非零长度、完整范围在 16 MiB 内，内部按至多 256 B 分块；多块之间让出一个 RTOS tick。 |
| `SRNOS_NORProgram(address, data, length)` | 1～256 B 且不跨页；调用者在需要 0→1 时先擦除；完成后逐字节回读比较。 |
| `SRNOS_NOREraseSector(address)` | 4 KiB 对齐；仅擦一个扇区；完成后回读完整 4096 B，分块间让出一个 tick。 |

调用者必须是成功占用板级适配的同一任务，且不在 ISR、屏蔽 IRQ 或锁住调度器的上下文。输入缓冲在调用返回前保持不变。接口不提供取消；服务收到错误后应离线并保留错误状态，不反复初始化或擦写。

首次 JEDEC 识别前先仅轮询 SR1，最多等待 500 ms，让 MCU 单独复位前已开始的 NOR 擦写完成；BUSY 始终不清除（包括读回全 `FFh` 的浮空状态）返回 `TIMEOUT`，不发 JEDEC 或写命令。就绪后才读取 JEDEC，未知 ID 返回 `UNSUPPORTED`；仅接受官方 W25Q128JV 表中的 `EF4018`、`EF7018`。它们不是完整订货后缀，不能据此证明实装封装、电压、温度等级或所有历史兼容型号。处在 QPI 或深度掉电的器件仍可能无法识别，此时保持离线；不自动发送复位、退出 QPI 或解除掉电命令。

初始化和每次写前读取 SR1/SR2/SR3。任意 BP、CMP 或 WPS 置位均返回 `PROTECTED`，即使本次地址可能位于未保护区；SUS 置位返回错误。不修改 BP、QE、SRP、OTP、安全寄存器或 individual lock。SRP 仅限制状态寄存器写入，不作为数组保护的替代判断。

每次写先发 `06h` 并确认 WEL，随后只允许 `02h` 单页编程或 `20h` 4 KiB 擦除；完成时确认 BUSY=0、WEL=0 并检查回读。WIP 等待每轮 `osDelay(1)`；编程等待预算 10 ms，擦除/就绪等待预算 500 ms。底层每个 HAL command/data 调用最多 5 ms，数据段至多 256 B。以上是局部等待预算，不是任务被抢占后的端到端延迟保证。

参数错误不锁死设备。初始化、读写的运行错误会锁住后续所有写操作，直到重启；板级 HAL 传输错误还会隔离后续所有传输，不 reset/abort/retry 部分写操作。返回失败时介质可能已经改变，服务必须以 CRC/提交记录判断有效性。

## 固定板级配置

生成代码先完成 OSPI1 的时钟、引脚和 OSPIM 路由。自维护适配确认 HAL READY、控制器非 BUSY 后禁用 OSPI，应用固定配置再启用，不重入生成 MSP。原因是本仓库 `HAL_OSPI_Init` 在 READY 状态不会应用新的 Init 字段。

- 先检查 OSPI 时钟 mux 必须为固定板的 `RCC_OSPICLKSOURCE_D1HCLK`，再通过 `HAL_RCC_GetHCLKFreq()` 读取源频率、向上取整分频，使 SCK ≤20 MHz。未知 mux、零频率或无法表示的分频在修改引脚/寄存器前返回错误；本仓库 HAL 的 `HAL_RCCEx_GetPeriphCLKFreq()` 不支持 OSPI，不能用于此处。
- Mode 3、单线 SDR、24 位地址、零 dummy cycle，读指令 `03h`。
- HAL `DeviceSize=24` 表示 24 个地址位；寄存器 DEVSIZE 编码为 23。它与旧 QSPI `FlashSize` API 的命名不同。
- CS high 设置 4 个 SCK 周期，至少 200 ns；保留 half-cycle sample shifting，关闭 DTR、DQS、DHQC、wrap、refresh 和 dual-quad。
- `MemoryType=MICRON` 是明确选定的非 HyperBus 控制器编码。Micron/Macronix 的区别用于 octal-DTR 数据顺序，不作为 Winbond 身份判断，也不将本器件切换成 Micron 协议。
- 保留生成 IO0/IO1/CLK/NCS 绑定；PA3/PA6 在此单线 profile 中改为输出高，固定 /WP、/HOLD 非有效。没有修改生成文件、存储器状态位或公共配置。

驱动只有少量状态变量与一份最多 256 B 的栈上验证缓冲，板级每次传输使用栈上 HAL command 结构；没有新增静态页缓存。服务应按整条调用链、HAL、TLS 与中断余量预算 worker 栈，不能只看 256 B 缓冲。

## 来源与硬件边界

- [Winbond W25Q128JV Rev F，2018-03-27](https://www.winbond.com/resource-files/w25q128jv%20revf%2003272018%20plus.pdf)：第 13～20 页状态/保护，第 21～25 页 ID 与命令，第 28/36/38 页读/页编程/扇区擦除，第 64～65 页时序。`03h` 上限 50 MHz，页编程最大 3 ms，4 KiB 擦除最大 400 ms，写后 CS high 最少 50 ns；本实现采用更保守的频率与等待预算。
- [ST QUADSPI/OCTOSPI FAQ](https://community.st.com/stm32-mcus-60/overall-faqs-for-quadspi-octospi-hspi-xspi-132640)：1.5～1.7 的单线、CS high 与采样配置，3.2～3.3 的 MemoryType 语义。
- 仓库固定 HAL：`Board/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_ospi.h` 和对应 `.c`；板级绑定依据 `Board/Core/Src/octospi.c`、`Board/OMNICTL2_3.ioc`。

有限次板端识别、编程/回读和 MCU 复位保持结果见 [CHANGELOG](CHANGELOG.md)。实装完整后缀、保护状态矩阵、电气波形、脏扇区擦除、满环回绕、100 Hz 长期负载、供电掉电恢复和寿命仍需对应资料或实板验证。这里不声称具备固件升级、Flash 分区保护或黑匣子格式恢复能力；它们由调用服务实现。
