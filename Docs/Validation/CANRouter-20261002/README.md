# CAN Router 每路 3 kHz 双向目标：离线优化

工作开始于 2026-10-02，最终离线验证于 2026-10-03。用户要求先 add/commit 当前工作区，再优化 CAN，并将 CAN 通信置于 IMU、UART 之前。原工作区已提交为 `820b777`（保存 SDK 重构、托管任务与持久化验证基线）；本轮优化尚未提交。PCB 不在线，未烧录、复位、连接探针或发送实际 CAN 帧。

本记录保存首次 CAN 优化的策略和证据；随后用户调整为 P0 CAN1/2/3＋IMU、P1 CAN4＋UART，当前配置与新构建见 [优先级调整](../Priority-20261003/README.md)。

## 目标与结果

目标为 CAN1–4 每路同时 3000 帧/s RX 与 3000 帧/s TX，软件共处理 24000 次收发/s。已完成针对通知、路由、SPI 事务与调度的优化，并修复高频并发路径中的确定缺陷。**离线测试和编译通过不证明本板已达到该吞吐。** Classic CAN 收发共享一条物理总线，线占用还取决于 ID/DLC、位填充、仲裁及重传；测试目标不能替代物理带宽预算。

保留八个 RX/TX worker、每线程栈、路由/one-shot/ring 容量、SPI 八槽、RTOS heap、DMA/BDMA 银行及公开 CAN API。电机公开函数签名保持，私有对象布局变化，需要父工程重新编译。adapter 发送周期仍按毫秒配置，不新增 333 µs 周期合同。

## 改动与调度影响

| 路径 | 本轮行为 | 验证要点 |
|---|---|---|
| CAN 唤醒 | 原生与 CAN4 改用 index 0 直接任务通知；Kernel 控制仍为 index 1 | 绕过 ISR EventFlags 的 Timer daemon 中转；注册前积压、等待交错与 CAN4 INT 双消费者分别测试 |
| 原生 RX | ISR 入口快照 FIFO 数量；SPSC ring acquire/release；满环丢新并计数 | ISR 不再修改消费者索引；满环已有数据、读失败及持续到达有界测试 |
| 路由/TX core | 冻结后二分直读订阅、去逐帧快照/临界区；无 adapter 快路径；group 暂存按需初始化 | 冻结 adapter/raw fanout、STD/EXT 隔离，多组转少组、RAW-only、urgent→periodic、shadow/token 检查 |
| TX 完成 | 每帧计数，每批只通知一次 | 8 帧完成等于 8 次完成计数与 1 次通知 |
| CAN4 SPI | 一次成功空位查询供紧随单次提交消费；去 RX 重复清事件 | 3000 次正常提交对应 3000 次准入 StatusGet；供应商提交内部读保留；错误失效、1 ms 重试/TTL 保持 |
| Bus-Off | 正确 HAL ErrorStatus 回调；TX worker 内 100 ms 退避、10 ms 旧 TX 取消确认、最多 100 ms 重入确认；最多 3 次失败后隔离 | 真实驱动与当前 HAL 提取的分派分支测试；实际硬件取消/入网尚未验收 |
| 电机反馈 | 六种电机短 BASEPRI 保护 RX/tick/快照，超时和 LK 满量程并发变量原子化；拒绝旧 tick 对新反馈的离线误判 | 六型并发读写、时间回绕、嵌套 mask、UBSan/TSan；无自旋与 RTOS 阻塞 |
| Release 编译 | 八个自维护 CAN/SPI 热路径文件使用 -O2 | Debug、供应商编译策略与浮点语义保持；未新增 LTO/fast-math |

| 执行上下文 | 当前优先级 | 说明 |
|---|---|---|
| SPI4 dispatcher | Realtime1 / 49 | CAN4 的依赖；回调必须短且不可阻塞 |
| CAN1–4 RX/TX | Realtime / 48 | 八线程同级，RX 批次 32 帧后续唤醒并 Yield |
| I2C4 dispatcher | High6 / 46 | 原值保持 |
| BMI/LSM、INS、Serial dispatcher | High5 / 45 | IMU 线程原值；Serial 分发提高，UART 协议线程保持原值 |
| FDCAN1–3、MCP EXTI15_10、SPI4/DMA IRQ | NVIC 5 | 满足 FromISR 5..15 规则；同级 IRQ 不嵌套 |
| LSM EXTI9_5、UART IRQ | NVIC 6 | 原值；UART DMA 的原 6/7 设置保持 |

SPI 在每笔事务边界优先选择 CAN；最多连续 4 笔 CAN 后服务一笔等待的非 CAN，非 CAN 内保留 4 IMU:1 普通事务。活动 DMA 不抢占、超时和取消规则不变。它防止排队选择永久饿死其他类别，但不证明实际 IMU 1 ms deadline。高优先级任务持续就绪仍可挤占低优先级任务，Yield 仅让出同级；需要组合负载实测 CPU 与采样间隔。移除七个 CAN EventFlags 对象后，保守启动同步预算没有缩减。

独立 review 发现并修复 CAN4 初始化失败时发布的任务句柄可能随任务退出失效的问题：现仅在 HwInit 成功后注册，失败后 IRQ 只记录 pending，并增加真实 RX/TX worker 失败路径回归。core 的冻结访问及 group 暂存另经独立复核。

## 离线验证

- `python3 Tests/run_host_tests.py`：28 套通过，包含新增 Native、External、Motors；四路虚拟 1 秒各 3000 RX＋3000 TX 核对顺序、数据、零填充、统计、TTL 和冻结 RX 零临界区。虚拟时钟/假硬件只证明工作计数和状态机行为，不是墙钟性能测试。
- `SRNOS_MOTOR_SANITIZERS=thread bash Tests/Devices/Motors/run_host_tests.sh`：六型并发回归通过。主机互斥替身只建模 MCU mask 互斥，不证明实际关中断时长。
- `python3 Tests/Build/check_sdk.py`：91 公共头、158 个 C11/C++17 编译及依赖单元、13 条父应用 include 隔离、十个父项目固件、第三方哈希、C++/TLS、容量覆盖/越界拒绝通过。
- 独立 `build/can-router-20261002/Debug`、`Release` 编译通过；两种 ELF 的段边界、DMA/BDMA、TLS、异常向量及五个 CAN 强回调门禁通过。未覆盖常用 `build/Debug`/`build/Release` 调试产物。
- 结构检查通过 505 项合同；Debug 当前 114 个 SDK 对象的命名检查通过。Release 命名检查仍有 13 条基线已存在的标准库模板/TLS errno 包装符号误报，见 `release-names-known-failures.log`，本轮未扩大放行范围或声称该检查通过。
- 16 个 CAN/SPI/IMU/UART IRQ 在 IOC 与生成代码一致；八个 CAN/SPI 编译单元的 Release 最终优化选项为 -O2，Debug 没有该覆盖。CRLF 感知的 `git diff --check` 通过。

CubeMX 使用 `python3 Board/generate.py` 于 2026-10-02 实际执行，project generate OK、I2C 时序检查通过。快照为 `build/cubemx/20261002-234803-dyqnjq5f`；生成内容仅 fdcan.c/gpio.c/spi.c 的指定 IRQ 优先级变化。415 个 USER CODE 区域逐字保留，Support/Linker/cmake/Drivers 不变。编译仍有已有 CMSIS `optimize` 属性被 ST Arm Clang 忽略的警告，未修改上游代码。

## 链接资源

以优化前同配置 Minimal Release 为对照，单位为字节。占用包括固定保留区，不等于 RTOS 实际空闲堆。

| 区域 | 基线占用 | 本轮占用 | 增量 | 本轮链接余量 |
|---|---:|---:|---:|---:|
| D1 | 254336 | 254432 | +96 | 73248 |
| D2 | 6848 | 6848 | 0 | 25920 |
| D3 | 544 | 544 | 0 | 15840 |
| ITCM | 63008 | 63104 | +96 | 2432 |
| Flash | 200608 | 212000 | +11392 | 836576 |

DTCM linker 仍覆盖 128 KiB（含 heap/MSP 保留区）；heap/MSP 容量和内存银行不变。Debug 的 D1 为 254400、D2 为 6848、D3 为 544、ITCM 为 63104、Flash 为 283488。构建工具链与 ELF SHA256 见 `artifacts.json`。ITCM 已接近容量上限，不宜无预算继续迁入数据或代码。

## PCB 在线后的验收

1. 先分别检查 CAN1–4 的时序和真实数据完整性，再同时施加每路 3000 RX＋3000 TX；包含真实业务 adapter/回调和突发，按独立设备序号核对交付，不只看提交成功。
2. 同时运行双 IMU/INS、实际 UART/DMA、CDC 遥测；记录 CPU/idle、最大 IRQ 和回调时间、线程栈水位、SPI 等待分布/超时、IMU 间隔、软件满环与硬件 overflow、TX 背压/过期。
3. 对原生 CAN 做 Bus-Off、断线恢复和持续故障测试，确认旧硬件帧不重放、三次失败隔离与恢复耗时；CAN4 的实际错误恢复也须独立验证。
4. 对长时间压力、EEPROM 并行、USB 堵塞及实际掉电复测。本轮不承诺 CAN4 物理 Bus-Off 自动恢复或高负载 IMU 连续性，旧 CAN1/CAN3 板测不能替代当前固件。

原始离线日志随本文保存。历史板测分别见 [CAN1/CAN3](../CAN13-ADC-20260928/README.md) 与 [ITCM 启动](../ITCM-20260929/README.md)，不改写为本轮硬件证据。

本次最初 OS 初检中发现、未混入 CAN 修改的功率限制/长微秒延时/Fault 地址校验/参数调试输出问题，见 [开发清单](../../Development.md#2026-10-03-初检遗留问题)。
