# 验证记录

本文保存对应归档的原始报告。日期、版本、通过范围和结束状态仅对当时现场有效，不表示当前板上状态。当前阅读方法见本目录说明文件。

---

<!-- 原报告：README.md；原文 SHA256：866d0db2096f20a5ed52f603f8763dd9c7de2a9bcc334470e4586bc7394c62da -->

# CAN Classic / FD 支持与离线验证（2026-10-03）

在 [CAN router 优化](../CANRouter-20261002/README.md) 和 [P0/P1 优先级](../Priority-20261003/README.md) 的基础上，四路控制器现支持启动时互斥选择 Classic 或 CAN FD。基线提交仍为 `820b777`，此后修改未追加提交或推送；PCB 离线，未烧录、连接探针或改动参考工程。

## 配置与实现

- `SRNOS_SystemConfig.can[0..3]` 对应 CAN1..4；默认均为 Classic 1 Mbit/s。应用在 `ApplicationConfigure` 明确设置 mode、nominal_bitrate、data_bitrate、brs，在 `bind_channels` 声明协议。最终兼容性检查成功才复制并冻结；错误恢复不改变模式。
- Classic raw 支持 125k/250k/500k/1M；现有 adapter 仅 Classic 1M。FD 通道上存在 Classic 路由、adapter 或预排队项，以及 Classic 通道上存在 FD 路由，均拒绝启动。FD 业务协议归父应用，未增加电机 FD adapter。
- FD+BRS 数据速率支持 500k/1M/2M/4M/5M/8M，必须不低于仲裁速率；无 BRS 时 data=nominal。`SRNOS_CANFDFrame` 为 72 B，长度仅允许 0..8/12/16/20/24/32/48/64，不自动补齐或截断。发送 BRS 与冻结策略一致，ESI 只接收；接收保留实际 EXT/BRS/ESI。硬件 FD 模式可能接收/ACK Classic，软件不把它交付给 FD 回调，短 FD 也不进入 Classic。
- FD 每路 32 条精确 ID 路由、64 个 TX 槽，低数值优先级先发、同级 FIFO、每轮最多32次提交；回调和硬件提交在锁外。TTL 默认20ms，显式1..INT32_MAX，限制进入 backend 前的软件队列等待；已开始的 SPI/HAL 提交和硬件 FIFO/仲裁重发不受它取消，不能用作线上交付截止时间。
- Native 使用原有128槽 SPSC RX ring和worker；为解决4KiB RX栈余量不足，每次最多搬运4帧，每轮仍处理最多32帧，满轮续通知并yield。CAN4保留单次FIFO空位token、1ms SPI失败重试、5ms RX兜底和初始化成功后才发布任务句柄。
- `tx_complete_events` 在 Native 统计 TEF逐帧完成，在 CAN4 统计确认的TX FIFO事件，可能合并，不可当作逐帧成功发送数；纯RX共享IRQ和事件读取失败不计入完成事件。

用法见 [CAN README](../../../Source/IO/CAN/README.md) 与 [CANFD 示例](../../../Examples/CANFD/README.md)。示例默认只接收 CAN1 FD 1M/4M；编译开关可演示一次12B测试帧，CAN2/3/4保持Classic1M。

## 硬件与资源

Native 原25MHz无法精确生成常用2/4/8M数据速率，故 IOC 的 PLL2Q 32→10，FDCAN kernel=80MHz；PLL2P=400MHz、PLL2R=40MHz不变。Classic1M nominal变为 prescaler1、seg1=60、seg2=19、SJW13，采样点76.25%（原76%）。应用配置由 `Board/Support/IO/can_native_board.c` 在控制器启动前应用；时钟、CCU、HAL初始化、TDC或滤波失败会阻止启动，不能悄悄回到MX默认Classic配置。

三路 Message RAM 固定偏移为0/0x300/0x600 word。Classic每路232words；FD每路676words，FIFO0 8×64B、TXFIFO28×64B、TEF8，不使用FIFO1/Rx buffer/dedicated TX；最后结束于8848B，小于共享10KiB。TDC采用 [ST官方例程](https://github.com/STMicroelectronics/STM32CubeG4/blob/master/Projects/STM32G474E-EVAL/Examples/FDCAN/FDCAN_Com_IT/Src/main.c) 的TDCO=DataTimeSeg1×DataPrescaler、TDCF=0，在BRS且数据速率>1M时启用。

CAN4使用MCP40MHz时钟，Classic mode6、32RX/32TX×8B；FD mode0、16RX/12TX×64B，共2016B，关闭TEF/TXQ/timestamp。仲裁和数据寄存器独立选择。无BRS的125k/250k使用不参与发送的数据时序占位1M。模式确认最多20次读回、间隔1tick，SPI等待也占时间，不能称为严格20ms硬截止。供应商实现未修改；驱动使用SDK精确DLC转换，避开vendor长度向上合并行为。

P0仍为CAN1/2/3+BMI/LSM/INS，P1仍为CAN4/UART；共享SPI49及IMU4:低层1、低层CAN4:普通4:1策略保持。任务数量、4KiB CAN栈、heap、Classic16B帧/59920B ITCM context和DMA银行不变；FD context为普通D1的23312B。

| 区域 | Debug | Release | 与优先级轮相比 |
|---|---:|---:|---|
| D1 | 277856 B | 277888 B | +23456 B，Release余49792 B |
| D2 | 6848 B | 6848 B | 不变 |
| D3 | 544 B | 544 B | 不变 |
| ITCM | 63104 B | 63104 B | 不变，余2432 B |
| Flash | 288128 B | 218720 B | 含新增FD实现 |

DTCM链接保留区仍128KiB，包含原heap、RX ring和MSP，不表示动态堆已用满。FD静态池对全Classic配置也保留空间，不在运行期申请额外heap。

## 验证结果

- 29套主机测试通过；FD核心11场景含20000步独立队列模型、互斥注册、完整长度/flags/ID/BRS、满队列、优先级/FIFO、TTL与时钟回绕。原四路各3000RX+3000TX虚拟时间回归继续通过，只证明软件数据与计数。
- Native测试链接真实controller和Board planner，覆盖8种三路混合模式、全部合法速率、DLC/64B、未冻结/clock/CCU/Init/TDC/filter失败闩锁、Bus-Off恢复保持配置。真实worker验证70帧按32/32/6处理，每次pop≤4。
- CAN4测试使用未修改vendor、真实device/RX/TX worker与SPI寄存器/RAM替身，覆盖全部速率、16种DLC、2016B RAM、模式确认超时、逐步SPI初始化错误、未冻结零SPI、长TTL换算、纯RX IRQ不计TX完成、FD/Classic分流及32帧续通知。替身不模拟模拟信号或真实RTOS时延。
- `Tests/Build/check_sdk.py`通过11个父项目固件（含CANFDExample）、93公共头/162个独立及聚合C11/C++17编译与依赖闭包、14个应用include隔离、117个当前SDK对象命名、上游哈希、C++/TLS、容量覆盖及越界拒绝。
- 独立 `build/can-fd-20261003/Debug`、`Release` 构建及ELF内存/TLS/向量/强CAN回调检查通过。ELF新增门禁锁定FD池在D1、Classic大小与ITCM位置保持。Release十个通信热路径文件最终-O2；既有Release标准库/TLS命名误报未修改或宣称通过，命名验证使用Debug对象。
- `python3 Board/generate.py`实际成功、I2C检查通过。快照 `build/cubemx/20261003-005600-fzxuj303`：生成器仅改fdcan.c、main.c和IOC，415个USER CODE区逐字保留；HAL/CMSIS、工具链、Linker不变。SDK之后完成了Support CAN planner。26项P0/P1 IRQ在IOC与生成代码仍一致。
- ST Arm编译器栈分析：RX worker自身 Debug512/Release504 B（原3536/3528），FD回调入口前静态调用链约912/896 B（原3928/3912），各减少3016 B。此值未包含应用回调深度、TLS与异常上下文，不替代板端栈水位验收。证据见 `stack/`。

日志摘要与ELF SHA256随本文保留；完整构建日志及产物位于独立build目录。生成文件沿用CRLF，`git -c core.whitespace=cr-at-eol diff --check`通过。

## 待板端验证

需要重新验证80MHz下Classic兼容性、FD所有实际使用速率/BRS/长度、外部收发器与线缆能力、断线/Bus-Off恢复、四路混合负载及IMU/UART时延和栈水位。FD在1M/4M、64B、合计6000帧/s时，仅payload已占数据相位76.8%的整秒预算，还需仲裁与协议开销；该示例速率不能被当作3k收+3k发满64B的保证。实际容量必须按协议帧长与速率验算后测量。

初次OS审查中非CAN遗留项仍记录在 [Development](../../Development.md)，本轮未将无关算法/诊断修复混入FD支持。
