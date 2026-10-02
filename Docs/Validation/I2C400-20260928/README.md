# I2C4 Fast-mode 提速验证 · 2026-09-28

先按用户要求将此前全仓库工作提交为 `ebef330`（`集成 INS、传感器遥测与 FT24C128A 共享 I2C 验证基线`），再进行提速。本次用户明确要求无需恢复 EEPROM 内容和固件。该目录记录后续尚未包含在基线提交中的修改与板测。

## 配置与边界

固定 I2C4 从 100 kHz 基线改为 400 kHz Fast mode，TIMINGR=`0x10C7182D`，PRESC/SCLDEL/SDADEL/SCLH/SCLL=1/12/7/24/45，PLL3R=60 MHz，模拟滤波开启、DNF=0。IOC 显式保存 Fast mode 与时序；Board/Glue 在启动阶段检查时钟源、PLL ready、实际 PLL3R 频率、滤波和总线空闲，再关闭 PE 写时序并开启 PE。未执行 CubeMX regenerate；生成文件仅 USER CODE 增加稳定调用，生成区旧时序随后由 Glue 覆盖。

按边沿各不超过 300 ns、模拟滤波延迟 50..80 ns 计算，最高 SCL 约 394.7 kHz，setup=433.3 ns、hold=233.3 ns，满足所选 Fast-mode 约束。依据为 [ST AN4235](https://www.st.com/resource/en/application_note/an4235-i2c-timing-configuration-tool-for-stm32f3xxxx-and-stm32f0xxxx-microcontrollers-stmicroelectronics.pdf) 和 [STM32H723 DS13313 Rev 5](https://www.st.com/resource/en/datasheet/stm32h723vg.pdf) 表 106。运行 `python3 Tools/Debug/check_i2c_timing.py` 可复核，输出保存在 `timing-check.json`。边沿条件是计算输入，未以示波器测量。

保留原 High6/3 KiB 唯一分发线程、四槽与周期保留槽、排队/活动期限、32 B EEPROM 读写/校验分段和 6 ms 写周期等待。没有修改生产线程数量、引脚、DMA、IRQ、MPU 或链接脚本。参数/HostLink/INS 存储仍未迁移。EEPROMBringUp 关闭融合与加热，BMI/LSM 及其他默认服务继续运行。

首次加载的候选版本在启动时钟检查处停止：本工程 HAL 的通用 `HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_I2C4)` 不支持 I2C4，返回 0。这次尚未进入 EEPROM 测试。改用 I2C4 时钟源检查和 `HAL_RCCEx_GetPLL3ClockFreq` 的 R 输出后重新构建、加载；最终进入 stage=1，读回 TIMINGR 正确、LIS/SPA READY。未修改第三方 HAL。

## 检查与方法

- 20 组主机套件全部通过，九个父项目固件、配置/链接/IRQ/heap/第三方哈希检查通过。日志为 `host-tests.log`、`sdk-check.log`。
- EEPROMBringUp Debug/Release 均编译链接通过。Debug：D1 316928 B、D2 11552 B、D3 512 B、Flash 254120 B；Release Flash 179336 B。D2 比原例程增加 576 B，主要为测试专用 SCL 观测结构及对齐；生产内存布局未改变。DTCM 128 KiB 显示全保留是静态划分，不代表 heap 耗尽。已有 CMSIS `optimize` 属性警告未改。
- 沿用唯一 ARM GDB/J-Link 会话、H723VG/SWD 4 MHz、探针 10114514。先读 EEPROM 并在 stage=1 保存 `eeprom-before.bin`，再显式设置 command=2；2 秒恢复等待、一次数字 SCL 观测、3 秒空闲基线后，连续执行负载。测量窗口无调试暂停或断点。
- 负载依次为 16 KiB 阻塞写/异步读、16 KiB 异步写/阻塞读、非对齐页及 0xFF/0x100 边界、高地址、末字节、非法范围；写后均逐段校验。最后整片读取供主机核对，跳过恢复写。
- Normal/2 KiB 测试观测线程每 1 ms 读取公开状态。阶段耗时按观测计数估计，约有毫秒级边界误差；样本率为 `(samples-1)/(last_us-first_us)`，短窗含相邻阶段首个旧样本，不视为精确逐样本实时验收。这里 `not_ready=0` 表示所有观测均处于 READY，与器件“尚无新数据”的正常计数不同。

## 测量结果

`stage=7`，**21 项通过、0 失败**，测试负载总耗时 13.703 秒，`restore_skipped=true`、`restored=false`。旧基线总耗时还包含恢复写，不能直接用两个总耗时比较提速幅度；以下比较相同操作。

| 操作 | 100 kHz 基线 | 本次 Fast mode | 提升约 |
|---|---:|---:|---:|
| 16 KiB 阻塞写，含等待与校验 | 8.192 s | 5.121 s | 1.60 倍 |
| 16 KiB 异步写，含等待与校验 | 8.191 s | 5.120 s | 1.60 倍 |
| 16 KiB 异步读 | 2.566 s | 1.031 s | 2.49 倍 |
| 16 KiB 阻塞读 | 2.566 s | 1.030 s | 2.49 倍 |

写入仍受器件内部编程等待和状态机推进间隔影响，读取也有每段任务唤醒开销，所以整体吞吐不会等比例提高四倍。参考基线见 [I2C-20260928](../I2C-20260928/README.md)。

| 工况 | LIS 约 Hz | LIS 最大样本间隔 ms | SPA 约 Hz | SPA 最大样本间隔 ms |
|---|---:|---:|---:|---:|
| 空闲基线 | 99.3 | 20.000 | 16.3 | 70.003 |
| 整片阻塞写 | 98.8 | 20.001 | 16.3 | 70.008 |
| 整片异步读 | 97.1 | 20.001 | 16.1 | 71.590 |
| 整片异步写 | 99.2 | 20.000 | 16.3 | 70.002 |
| 整片阻塞读 | 98.1 | 20.002 | 16.3 | 70.001 |
| 边界测试 | 100.0 | 10.896 | 16.2 | 69.099 |
| 最终整片读 | 98.0 | 20.006 | 16.4 | 69.999 |

全部阶段两传感器公开状态均 READY。负载窗口 LIS 新增 1353 样本、SPA 224 样本，错误/恢复/重初始化均无增长。周期事务新增 5670 次、后台 3672 次，均全部完成；两类错误、超时、取消、队列满均为 0，无总线恢复、隔离或回调队列溢出。累计最长周期排队 894 µs、后台排队 101 µs、后台传输 909 µs，包含启动/备份阶段；100 kHz 基线分别为 3349/1413/3355 µs。队列高水位 1，本次不是满队列公平性压力实验。

PB8 保持 I2C AF，GPIO IDR + DWT 记录 128 个相邻数字上升沿周期，核时钟 550 MHz。周期中位数 1482 cycles（约 371.1 kHz）；127 个主要分布样本为 1482..1522 cycles，均值 1494.22 cycles（约 368.1 kHz），可描述为实际约 **370 kHz**。全部样本均值 1501.44 cycles，另有一个 2418 cycles 的长间隔。此软件采样无法区分长间隔来自中断、漏采、伸展或事务间隙，不按该离群值推导线频；原始值完整保留。该测量不能代替示波器的上升/下降时间、幅值或裕量检查。

最终 CFSR/HFSR 均为 0，空闲 heap 93608 B。MCU `.isr_vector`、`.text` 与最终 Debug ELF 匹配，无断点。

## EEPROM 内容、产物与结束状态

初始全片均为 0xFF。最终内容为整片 seed=191 的模式，顺序叠加 0x003F/259 B、0x00FF/259 B、0x1FF3/259 B、0x3FFF/1 B 的 seed=1..4 模式。主机独立生成期望字节，与最终读回的 16384 B 全部一致；FNV-1a=`2949387238`，与固件发布值一致。

- 初始 EEPROM SHA-256：`0fbba07a833d4dcfc7024eaf313661a0ba8f80a05c6d29b8801c612e10e60dee`。
- 最终 EEPROM SHA-256：`7db3ccd567820a9ef9d177d8065dee54755a29d9a7e79a110be27f777380f971`。
- 已加载 Debug ELF SHA-256：`cc042d78c69236f0ca63655dae3ff5815cc8dc13d8efa729fffdd6d71cb24cbd`。
- Release ELF SHA-256：`da62b24341c1bc10095220c4ebb56681361f4a3ea5fef31968355a813c68c126`，仅构建，未加载板端。

`gdb.log` 含候选时钟检查失败及修正后全过程；`session-takeover.log` 记录接管现场；`continuity.bin`、`scl.bin`、EEPROM 前后转储、`summary.json` 和构建/主机日志保留。运行 `python3 Docs/Validation/I2C400-20260928/analyze.py` 可从冻结日志及二进制重新生成 summary 并核对最终数据。

按用户要求，**EEPROM 测试内容与 Fast-mode EEPROMBringUp 固件均保留**。MCU 已继续运行，GDB/J-Link 仍连接并占用探针，断点列表为空。融合、加热和上层存储关闭；测试结束后不再写入，复位后只读备份并等待显式命令，不会自动循环擦写。本次是正常共享总线短测，未覆盖温度/电压角落、长时间耐久、断线、卡总线、掉电或真实业务存储接入。

后续已按用户要求执行 CubeMX CLI Generate，使生成代码中的 TIMING 与本次已板测值一致；该后续工作未重新烧录，记录见 [CubeMX-20260928](../CubeMX-20260928/README.md)。
