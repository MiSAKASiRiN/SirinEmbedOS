# srneos 体系与命令合同（设计稿）

本文保留体系设计合同与后续扩展方向。0.1.0 已实现的命令、参数及限制以 [README](README.md) 为准；本设计中超出当前实现的能力不能当作可用接口。命令名固定为 `srneos`，Python 包名为 `srneos`；不改变 MCU 的 `SRNOS_*` 公共符号。

当前实现选择 SEGGER 动态库/PyLink 子进程后端替代 Commander 输出解析，仍可生成结构化读取脚本。当前 run 为单会话 USB 编排，保存采用初始 manifest + manifest-final 两份不可变记录，统一遥测导出 JSONL；源码安装需要完整 SDK checkout，wheel 可独立安装。其它差异与未实现扩展在 README 明确列出。

## 1. 职责与已有基础

| 内容 | SDK 负责 | 父项目负责 |
|---|---|---|
| 连接与烧录 | 工具发现、J-Link 调用、目标识别、镜像段解析、回读与保存 | 固件路径、板卡选择、运行/暂停的期望 |
| USB | 设备/接口识别、独占串口、COBS/CRC、请求响应与订阅 | 启用需要的 SDK 服务 |
| 遥测和命令 | 内置流解码、SOURCE/参数目录、EXEC/CONTROL 编码、采集 | 注册应用字段、命令和控制邮箱，提供业务含义 |
| SirinRMT | v1 编码、会话/序号、有限时发送、停止收尾 | 使能权限、控制映射、速度限制、失联行为和执行反馈 |
| 主机测试 | 通用清单、运行器、超时、结果汇总 | 应用自己的测试与验收判据 |
| 证据 | 格式、原始数据、时间、哈希、失败与完整性 | 本地存放位置、实验备注、对业务结果的解释 |

SDK 不引用父项目 App 私有结构、固定电机数量或任何个人设备地址。当前硬件范围仍是 SDK 的 H723/OmniCtrl 平台，不为历史使用过的 F072 增加整套板型抽象。探针自身的型号/固件名称不能用作目标 MCU 身份。

当前来源及可复用内容：

| 来源 | 现有合同与迁移方式 |
|---|---|
| [USB 服务](../../Source/Services/USB/README.md) | CDC0 ASCII 日志、CDC1 SirinLink；保留分工 |
| [线协议](../../Docs/SirinLink.md) | V2 header/COBS/CRC、HELLO、EXEC、CONFIG、CONTROL、TELEMETRY |
| [imu_telemetry.py](../SirinLink/imu_telemetry.py) | 提取纯 codec、流式 Decoder、内置传感器解码 |
| [imu_capture.py](../SirinLink/imu_capture.py) | 保留逐样本、原始 RX、质量统计与不自动拼接断线数据的行为 |
| [system_monitor.py](../SirinLink/system_monitor.py) | 提取 SystemMonitor 解码；统一请求器及退出码 |
| [sirin_rmt.py](../SirinLink/sirin_rmt.py) | 复用 30 B v1 与 EVENT 封装；升级会话编排和收尾 |
| [Input/SirinRMT](../../Source/Services/Input/README.md) | 输入不是执行器授权，ENABLE/DISABLE 不等于执行结果 |
| [开发流程](../../Docs/Development.md) | SDK 主机套件是可选私有内容，缺失不能报告通过 |

首版不修改固件协议。HELLO 当前仅返回版本、容量、能力位等 12 B 正文，不含完整固件哈希，也不提供所有 EXEC/业务控制的动态目录。设备身份可尝试 CONFIG IDENTITY，服务关闭或身份缺失须单独报告；不能因此否认已经成功的 HELLO。未知功能返回 unsupported/not_found，不臆测支持。

## 2. 实现结构与交付

主机端采用 Python 3.11+，参数解析、TOML、文件、进程和 JSON 优先用标准库；串口层使用现有 pyserial。通过 Python packaging 的 console script 暴露 `srneos`，同时提供 `python -m srneos`。J-Link 使用用户已安装的软件，不捆绑供应商二进制，不自动安装/降级或解锁 MCU。

计划目录如下；本设计阶段不建立空代码模块：

```text
Tools/CmdLineTool/
  pyproject.toml
  src/srneos/
    cli.py                 # 命令解析和统一退出
    config.py              # 明确装载配置，路径解析和校验
    result.py              # 状态、步骤、错误与 Schema
    artifacts.py           # 事务式保存、原始记录、哈希
    jlink.py                # 探针/目标、Commander 模板及结果解释
    images.py               # ELF/HEX/BIN 段与 Flash 地址合同
    usb.py                  # USB 清单、CDC 识别、串口所有权
    sirinlink.py            # codec 和单会话请求/事件分发
    telemetry.py            # 订阅、流解码、质量统计
    commands.py             # EXEC/CONTROL/参数
    rmt.py                  # 输入会话、序号和有界运动发送
    host_tests.py           # 显式清单与子进程运行
    runs.py                 # 同一进程的多步骤编排
  examples/
  schemas/
```

SDK 持有可安装工具、内置协议及通用运行器；父项目只需在自己的虚拟环境安装该 SDK checkout 的工具包，提供 `--config PATH` 即可调用。工具不依赖调用者 cwd 恰好是 SDK 根目录，不参加 MCU 的 CMake 源码扫描。

配置优先级：CLI 显式参数 > `--config` 指定文件 > SDK 默认值。首版不向上递归搜索并执行任意项目配置，不静默读取个人历史设备地址。配置内相对路径以配置所在目录为基准；CLI 相对路径以调用 cwd 为基准。结果记录最终解析值及来源，敏感字段不复制到共享文档。

首版支持同一套命令合同，分别实现 macOS、Linux、Windows 的设备发现和互斥适配。未经对应系统验证不能标记该系统已支持；缺少 USB 枚举后端时明确报 unknown，不以串口列表为空冒充 USB 未枚举。

## 3. 公共调用规则

```text
srneos [--config FILE] [--json | --jsonl] [--output DIR] [--timeout SECONDS]
       [--dry-run] <command> ...
```

公共参数允许位于子命令前或后，帮助信息显示实际适用范围。不支持的组合直接拒绝，不静默忽略。

- `--json`：stdout 最后输出一份结果对象，进度只在 stderr；长采样数据放保存文件。
- `--jsonl`：stdout 每行一个带 `schema_version/type/run_id` 的事件，类型为 state、progress、sample、result；最后一行 result 的 result 字段符合结果 Schema。原始二进制不混入 stdout。
- `--output DIR`：创建新的运行目录并保存所有本轮证据；目录已存在即失败，不覆盖旧记录。
- `--save FILE`：为支持读取的命令保存主要载荷；Flash 是原始 bin，地址读取为带类型的 JSON 或显式 bin，日志为原始字节，遥测为明确指定的 jsonl/csv。不把它解释为设备持久化。
- `--output` 与 `--save` 可同时使用：前者放证据，后者导出主要载荷。每个文件独立记录哈希和保存状态。
- `--timeout`：单次请求/步骤的有限期限；另设整体运行期限和有界收尾期限，不允许无限等待连接。
- `--dry-run`：仅离线校验、展开镜像段、请求编码和操作计划；不打开串口、不连接探针、不启动硬件测试。结果 `status=planned`，在线状态为 not_checked，绝不报告验证通过。
- 非交互为默认，不等待 stdin 确认；需要改变运行状态的行为用明确参数表达，缺失则返回可诊断的 precondition_failed。
- `connect`、`flash`、`verify`、`read` 为一次性操作，会在**本次操作内**重新检查设备身份和状态。上次 connect 的成功不是下一次动作的通行证。

`doctor` 额外检查工具路径/版本、配置、输出目录可写性和当前资源占用；不自动接管 IDE、复位板卡或解除读保护。`--version` 及 `capabilities` 返回工具版本、Schema 版本、安装的后端与已实现命令，Agent 不靠猜测调用尚未实现的功能。

## 4. 连接状态模型

连接分层记录，禁止只返回一个 connected 布尔值。每层都有状态、观测时间、证据和错误原因；未知与离线不同。

### 4.1 J-Link

```sh
srneos connect jlink --probe SERIAL --json
srneos connect jlink --probe SERIAL --target --device STM32H723VG --json
```

不带 `--target` 只枚举/检查探针可用性，不进行 SWD attach；目标为 not_checked。带 `--target` 才尝试目标调试连接。两种调用都是单次检查，不留下假想的跨进程连接。

| 探针状态 `jlink_probe` | 目标状态 `target` | 含义 |
|---|---|---|
| offline | not_checked | 可用枚举/访问证据表明所选 J-Link 不在线 |
| online | offline | 已能与 J-Link 通信，但在本次明确的目标连接尝试中目标不可达 |
| online | online | 目标调试访问成功；记录芯片 ID、可取得的 UID 与当前运行状态 |
| enumerated/busy/permission_denied/unknown | not_checked/unknown | OS 看见探针不等于可用；占用、权限、工具缺失不能硬归类为离线 |
| online | protected/mismatch/unknown | 读保护、目标不符或证据不足；与物理断线区分 |

`target=offline` 表示本次调试通道不可达，不断言 MCU 没供电或没运行。VTref、SWD 错误等是原因线索；未获得证据时不猜测根因。core=M7 不能单独证明型号匹配；UID 只有完成正确目标识别后才读取。

未指定探针且存在多个候选时返回 ambiguous_device；绝不选列表第一个。探针序列号和目标 UID 分别存储。传输先做本地 J-Link；远程 J-Link 可作为相同状态合同的后续后端，不默认扫描网络。

首版连接策略禁止隐式 reset/unlock/connect-under-reset；如果供应商某操作无法保证该策略，标记 unsupported_policy 并停止，而不是执行后宣称无扰动。`--connect-mode under-reset` 是显式可选恢复动作；不能由普通失败自动升级而来。

### 4.2 USB 与协议

```sh
srneos connect usb --list --json
srneos connect usb --usb-serial SERIAL --json
srneos connect usb --cdc1 CDC1_PATH --handshake --json
```

| USB/CDC 状态 | `sirinlink` 状态 | 面向用户的结论 |
|---|---|---|
| usb=not_enumerated | not_checked | 当前选择器对应 USB 未枚举 |
| usb=enumerated, cdc1=missing | not_checked | USB 枚举成功，CDC1 串口接口尚不可用/驱动未就绪 |
| usb=enumerated, cdc1=open | no_response | 端口可打开，在期限内没有有效 HELLO 响应 |
| usb=enumerated, cdc1=open | invalid_protocol/incompatible | 收到数据但帧校验、响应匹配或版本不满足合同 |
| usb=enumerated, cdc1=open | ready | 本会话 HELLO 响应校验成功 |
| usb=unknown 或 cdc1=busy/permission_denied | not_checked | 枚举后端、访问权限或占用问题，不能判作没有 USB/没有协议 |

用户要求的“枚举成功但没有协议”归纳为 no_response/invalid_protocol/incompatible，并保留原因。超时只证明未收到预期响应，不证明固件没有协议实现。`connect usb --list` 成功表示列举完成，即使结果为空也可 `status=ok`；指定设备的 connect 未达到要求状态则失败。

CDC0 无握手，`cdc0=open`、`log_state=quiet/receiving/disconnected` 独立记录，`log_protocol=not_applicable`。安静窗口不等于日志口失效。禁止往 CDC0 发送 HELLO 或将日志文本当作协议响应。

优先使用 USB 序列号、父 USB 设备与接口描述匹配 CDC0/1，不依赖 COM 号或 `/dev/*` 排序。当前描述符为 VID 0x0483、非 DFU PID 0x52A4，接口字符串分别为 LOG 和 SRN-LNK（源码保留旧品牌字样）；这些是发现线索，不是唯一身份。缺少可靠接口元数据时要求显式 `--cdc0/--cdc1`，不得轮流向未知串口发送数据。

HELLO 以 SYSTEM 0x00/0x01、空正文请求；验证 COBS、CRC、kind=RESPONSE、service/message/sequence、status=OK、正文版本/长度/容量。忽略无关遥测并统计损坏帧。HELLO ready 不代表 SirinRMT HOST 已启用、传感器健康或业务命令存在。

J-Link 与 USB 独立：调试口不可用时 USB 仍可工作；目标在线但固件未运行时 USB 可能不枚举。只有取得可靠共同身份才把两条通路合并为同一板卡；USB 序列号不直接冒充 MCU 原始 UID。

## 5. 烧录、Flash 验证、地址读取与本地保存

```sh
srneos flash --probe SERIAL --image firmware.elf --after run --output build/srneos/flash-01
srneos flash --probe SERIAL --image firmware.bin --address 0x08000000 --after halt
srneos verify --probe SERIAL --address 0x08000000 --length 4096 --save flash.bin
srneos verify --probe SERIAL --image firmware.elf --output build/srneos/verify-01
srneos read --probe SERIAL --address 0x1FF1E800 --type u32 --count 3 --save uid.json
srneos read --probe SERIAL --address 0x24000000 --type u32 --count 16 --save ram.json
srneos read --probe SERIAL --request-file reads.toml --output build/srneos/read-01
```

以上地址是 SDK H723 的读取示例；读取范围是否适用于当前目标仍要由已核对的板级描述及地址类型检查。`reads.toml` 的设计结构为 `schema_version=1` 加 `[[reads]]` 数组，每项含 name/address/type/count；不能嵌入任意 Commander 指令。

共同执行链：离线校验输入和输出路径 → 锁定指定探针 → 检查占用 → 连接探针 → 识别目标 → 检查芯片/可选预期 UID → 记录初态 → 执行动作 → 检查结果 → 执行指定收尾 → 保存最终状态。设备状态沿用第 4 节，不将烧录器退出码代替目标状态。

**flash**：只编程明确的 Flash load 段。BIN 必须有地址；ELF/HEX 解析所有实际 Flash 段，拒绝越界、重叠矛盾、截断或不支持的布局。ELF 使用物理 load 地址识别 Flash 中的 RAM 初值，不比较 BSS、NOLOAD、调试节和未定义空洞。首版借助可验证的 ELF 解析库或明确工具版本实现，不用文本 grep 猜地址。

每次 flash 成功必须独立读取实际已编程区间逐字节比较，记录 expected/actual SHA256、区间和首个差异；J-Link “download successful” 只是步骤证据。擦除影响按 Flash 扇区计算，计划列明受影响扇区；若段未覆盖整个擦除扇区，默认拒绝可能破坏段外数据的操作，要求明确选择保存/恢复段外数据或允许擦除该范围。选择保留时，段外原字节也必须回读一致才算成功。不得假定下载器会保留校准/参数。读保护阻止前置回读时报告 protected；不自动 mass erase 或解锁。

烧录必须明确 `--after run|halt`，可来自显式配置；会先扰动/暂停目标，不能承诺保持原运行过程。验证失败时不启动新镜像，保留可判定的暂停状态并报告实际状态。烧录/校验超时记录可能部分写入，不自动重烧。可选 `--backup DIR` 在写入前备份全部受影响扇区，备份未完整落盘则不写；回滚另行显式调用，不在失败后猜测性恢复。

**verify** 按用户语义首先是读取 Flash 内容；不要求必须提供镜像：

- `--address/--length`：读指定 Flash 范围，返回读取结果/哈希；`comparison=not_requested`，不显示“镜像一致”。
- `--image`：按镜像定义的 Flash 段读取并比较；BIN 同样需要地址。与手工范围选择互斥，消除覆盖范围歧义。
- `--address/--length --expected-sha256 HASH`：完整读取该范围后比较哈希。
- `--save`：只保存一段连续范围；多段镜像需 `--output` 保存分段 bin 和 manifest，不擅自填充空洞拼成一个文件。

**read**：按 u8/u16/u32/u64/i8/i16/i32/i64/f32/f64 或 bytes 解释地址内容，默认 little endian；先验证范围、对齐、数量和总字节上限，再生成版本受控的 J-Link Commander 读取脚本。保留原始字节、地址、类型、解释值和工具输出。浮点非有限值用 null 配合 raw_bits/class 表达，JSON 不输出 NaN/Infinity。大结果保存在文件，终端仅预览并明确是否截断。

Flash/普通 RAM 使用读取模板；外设寄存器可能 read-to-clear，未知/MMIO 范围需显式 `--allow-mmio`，不把所有 read 宣称为无副作用。读运行中 RAM 是非原子观测，标记 `consistency=live_non_atomic`；`--halt` 明确请求暂停快照并注明时序扰动及恢复策略。无 `--halt` 的读取不得自动回退到暂停模式。

首版不接受任意 J-Link 脚本作为“只读”输入。结构化读取清单生成可审计脚本；未来如加入 raw-script，它必须是单独高级执行命令，标记副作用未知，不能复用只读保证。

**save** 是主机文件动作，单独返回 `not_requested/succeeded/failed/partial`；读成功但磁盘满必须保留 read 成功事实并整体非零退出。文件先写唯一临时文件、检查长度/fsync，再无覆盖地发布最终文件；必须采用原子 no-clobber 机制，不用可能覆盖旧文件的普通 rename 假装安全。部分数据保留 `.partial` 与失败清单，不冒充完整 bin。

## 6. 日志与遥测

```sh
srneos log --cdc0 CDC0_PATH --duration 30
srneos log --cdc0 CDC0_PATH --duration 30 --save cdc0.log
srneos telemetry list --cdc1 CDC1_PATH --json
srneos telemetry read --cdc1 CDC1_PATH --streams system,ins --timeout 5 --json
srneos telemetry capture --cdc1 CDC1_PATH --streams bmi,lsm,ins --duration 60 --output build/srneos/capture-01
srneos telemetry capture --cdc1 CDC1_PATH --sources source_name --hz 50 --duration 10 --save source.jsonl
```

`log` 只读 CDC0，原始字节与解码文本分开处理，文本解码失败不丢原始数据。正常达到时长可以成功且无日志；可用 `--require-data` 把无日志作为失败判据。

`telemetry list` 查询可发现的 SOURCE 目录并列出 SDK 已知专用流；已知流不等于当前固件已启用，标记 supported/unsupported/unknown，不为列举而默认全订阅。SOURCE V1/V2 明确协商；V2 订阅率遵守目录 maxHz、5 Hz 步长和协议容量，专用流按各自合同处理。

`read` 临时订阅并等待每个请求流/源至少一条满足有效性和年龄要求的新数据，然后退订；缓存旧值不是新样本。超时返回部分数据及缺失集合。`capture` 指定时长连续采集，`--duration 0` 只允许显式交互无限采集；Agent 配方使用有限时长。

一次 CDC1 打开完成 HELLO 后，单 RX 分发器同时处理请求应答和遥测，不能由两个 reader 各取一半字节。SOURCE 下标、参数 stable_id、二进制 stream_id 和 EXEC command_id 分别命名。未知专用流可保存 raw payload，不按相似长度猜结构。

串口 I/O、发送定时/取消处理与磁盘写入之间使用有界队列；写盘任务不持有串口锁，不同步阻塞发送截止时间。队列耗尽时标记数据不完整并终止严格采集/运动计划，不静默丢样继续报告成功。运行器需监控实际发送间隔，主机线程调度不提供硬实时保证。

保存：原始 `cdc1_rx.bin`、`cdc1_tx.bin`、带 monotonic 时间/方向/offset/length 的 `io.jsonl`、逐样本 jsonl/csv、首尾状态、订阅结果、计数与质量问题。设备采样时间和主机接收时间分开；整数时间戳/64 位标识在 JSON 用十进制字符串保精度，不插值补帧。目录与流版本保存到 manifest，未知字段可追溯。

正常结束取消本会话实际成功订阅的集合；退订失败单独报告。断线/复位/epoch 变化不自动拼接为一段连续成功采集；退出并保存部分记录。`complete=true` 只表示请求的时长/收尾完成；缺序、无效值和实际接收率另由 quality/判据决定。成功采样不等于板端控制功能通过。

## 7. 通用命令与参数

```sh
srneos command exec --cdc1 CDC1_PATH --id 0x0100 --payload-hex "" --json
srneos command exec --cdc1 CDC1_PATH --name app.read_version --json
srneos command control --cdc1 CDC1_PATH --channel 1 --generation 1 --deadline-us 50000 --payload-file command.bin
srneos parameters list --cdc1 CDC1_PATH --json
srneos parameters read --cdc1 CDC1_PATH --stable-id 0x1234 --save parameter.json
srneos parameters write --cdc1 CDC1_PATH --stable-id 0x1234 --value 1.5
srneos parameters persist --cdc1 CDC1_PATH
```

EXEC 经 service=1/message=1 发送 `u16 command_id + args`；参数长度遵守当前 HostCmd 更小容量，不只检查 480 B 外层上限。`--name` 从父项目显式配置解析为 ID 与编码，SDK 负责验证格式，不能把业务名称硬编码到 SDK。无通用 EXEC 枚举能力时，不提供伪“自动发现所有命令”。命令响应可 `--save` 到本地，保留 raw 和 status。

响应必须匹配三元身份并检查 status；ACCEPTED 是进行中，继续等待最终响应。超时/断连可能已经执行，返回 `execution=unknown`；不以新 sequence 自动重发有副作用命令。即使同 sequence，完成重放缓存只有 4 项/2 秒且会被淘汰，通用写命令默认不自动重发。只有已声明纯查询或业务幂等合同的命令才允许有界重试，记录实际次数。

CONTROL 与 SirinRMT 是 EVENT，现有协议没有通用执行 ACK。发送结果最多是 `sent`；要确认应用动作必须消费应用提供的反馈。deadline_us 是业务元数据，不是 SDK 代执行停止的定时器。

参数 WRITE 只改允许写的业务值；`persist` 才发送设备 SAVE，`restore` 才发送设备 LOAD。主机 `--save` 永远只保存本地文件。参数目录校验类型/范围/稳定 ID，SOURCE 遥测不可写，不提供通用写 RAM 绕过参数合同。IMU 校准等持久化命令可后续独立加入口，不能隐藏在连接/健康检查里。

## 8. SirinRMT 使能与移动测试

```sh
srneos rmt enable --cdc1 CDC1_PATH --duration 1 --hz 50 --output build/srneos/enable-01
srneos rmt move --cdc1 CDC1_PATH --vx 0.1 --vy 0 --wz 0 --duration 0.8 --hz 50 --enable --output build/srneos/move-01
srneos rmt channels --cdc1 CDC1_PATH --values 0,0,0,0,0 --duration 1 --hz 50
srneos run --plan motion.toml --output build/srneos/motion-01
```

默认走 CDC1，封装为 SirinLink V2 EVENT service=0x21/message=1，正文为现有 30 B SirinRMT v1。旧 UDP/UART 工具保留，统一 CLI 的首批验收以 CDC 为主；之后复用同一编码器扩展 UDP，不另造控制协议。

一个进程持有串口、非零随机 RMT session 和所有 kind 共享的 uint32 sequence。新建命令不重复使用旧会话文件中的序号；重连不自动 re-enable。一个 SirinLink 会话与 RMT payload 的 session 不是同一个概念，结果分别记录。

`enable` 是有限时的零输入使能测试：建立明确的输入类型和有效零值，持续发送至少配置的 qualification_ms，再发送一次 ENABLE；其后保持新鲜零输入至期限，最后停止。`move --enable` 在同一个会话中完成零值预热、一次 ENABLE、限时目标、停止和采样；不先发非零目标预热。`--enable` 未给出时仅发送输入，不隐式授予意图。接收应用可能此前已经使能，因此不宣称省略该参数就绝对不会运动。

协议层检查有限 float32、通道百分比范围、时间和频率；业务速度范围来自父项目配置，缺少限速配置时拒绝实际非零 `move`，允许 dry-run 编码。资格等待不代表业务使能已接受；当前脚本的 100 ms 只是可配置默认建议。首版 duration 最大 60 s、发送频率 1–100 Hz；使能测试 hz 至少 20，duration 和握手/预热/动作/收尾期限分别计算并记录。

所有正常到期、Ctrl-C、反馈失败和可处理异常都进入有界停止阶段：保持原运动类型发送零目标 → 发一次 DISABLE → 留出配置的接收应用消费窗口并保持零目标 → INVALIDATE → 关闭本会话。发送和反馈各有截止时间，DISABLE 的失败不被 INVALIDATE 掩盖；断线时报告 `stop=unconfirmed`，不保证软件 finally 能应对拔线、kill -9 或主机掉电。最后的停止保障来自应用自己的输入过期撤权。

不要把三个独立命令 `enable` → `move` → `disable` 当作同一会话脚本。跨步测试使用 `run` 的单进程会话。外部终止本机活动运行使用 `srneos cancel --run-id ID`：经带 owner/启动标识的本地控制通道请求原进程收尾，不能由第二个进程抢串口。首版不启动常驻系统守护进程。

独立 `rmt disable` 仅在明确知道仍有效的 RMT session、后续 sequence、端口可独占时发送一次请求；无法证明当前所有权则拒绝，绝不以新 session 发 DISABLE 后宣称停机成功。USB DTR 变化可能改变上层会话，不承诺这种跨进程方式可靠；优先在持有连接的原进程内停止。

通过 `run` 可在同一 CDC1 上订阅反馈并发送输入。父项目配置声明反馈 source/stream、数据新鲜度、enabled/实际速度字段及判据；SDK 运行器只解释有类型、无任意 eval 的比较/范围规则。需要 request/session 对应关系而现有字段没有时，报告关联证据不足，不把任意“enabled=true”当成对本次请求的响应。

结果明确区分 `sent`、`observed`、`passed`：只有发送证据时不能给 motion passed。反馈包括启用状态变化、动作期间观测及停止后观测；相应字段缺失时 `validation=not_evaluated`，若运行计划要求验收则整体失败。控制坐标固定 vx 前、vy 左、wz 逆时针，单位 m/s、m/s、rad/s；SDK 不写死具体机器人的通道映射。

## 9. 会话、编排与主机测试

资源互斥键为探针序列号或物理 USB 身份+接口；OS 锁与进程标识共同防止并发占用。锁目录在本机用户运行时目录，metadata 含 PID 和进程启动标识。残留锁只在确认 owner 已退出后清理；PID 存在但启动标识不符不认为同一个进程。其他 IDE/串口程序不会遵守 CLI 锁，必须同时处理 OS 独占失败和供应商 busy，不能杀进程抢占。

J-Link 命令退出时报告 `before/after=running|halted|unknown` 和 probe released 状态；读取结束不可无条件 reset/go。若 backend 不能确认保留初态或退出副作用，拒绝 preserve 策略或返回未知，不能承诺现场未变。对于多通路同板动作，run 串行执行 reset/flash 与 CDC 会话重建，禁止烧录同时发送运动；脱离该进程的外部复位不可保证阻止，检测断连即失败收尾。

`run --plan FILE` 读取静态 TOML 步骤，首版只支持有界顺序、失败后停止及 finally 清理；不做任意脚本语言、无限循环或通用工作流平台。所有配置在打开设备前校验；phase 可引用已打开会话，启用期间 RX 分发/发送/超时处理必须持续运行，不能因另一步等待而阻塞停止路径。

计划语义：`schema_version=1`、`[[steps]]`（唯一 id、operation、参数、timeout）、可选 `capture`（全程订阅）、`finally`（仅允许当前会话停止/退订/释放）。operation 是 CLI 已实现操作白名单。步骤引用前序结果使用显式类型字段，不执行字符串插值后的 shell；未实现 operation 在预检失败。证据按步骤归档，失败后未执行步骤记 not_run。烧录成功只解锁后续观察步骤，不自动判为整轮测试通过。

```sh
srneos test host list --suite-root LOCAL_SDK_TESTS --json
srneos test host run --suite-root LOCAL_SDK_TESTS --suite Services/Input --output build/srneos/host-01
srneos test host run --manifest host-tests.toml --output build/srneos/host-all
```

SDK 通用运行器支持当前本地 `Tests/<suite>/run_host_tests.sh`，清单显式列出选中的 suite；不递归执行目录中所有脚本。SDK 默认定位自身 checkout 的 Tests（可缺失），绝不误调用父项目同名目录。也支持外部 `--suite-root` 或显式 manifest；父项目应用测试通过同一运行器调用，不把套件源码搬入 SDK 公开树。

清单合同：`schema_version=1`，`[[tests]]` 每项 id、argv 字符串数组、cwd、timeout_seconds、可选 env 白名单；路径相对清单解析，`shell=False`，不允许设备返回数据形成执行命令。测试清单属于用户信任的本地代码，CLI 不声称能沙箱隔绝其硬件访问；host 测试的无硬件属性须由维护者和验收验证。

缺套件为 `suite_unavailable`，零测试为 `no_tests`，不得 skipped 后整体 PASS。逐套件记录 passed/failed/timed_out/not_run、退出码和日志。总体只有所有请求项通过才通过；超时终止该测试进程组并保存日志，运行器继续还是 fail-fast 由显式参数选择。编译成功、主机模拟通过、实机通过是三个独立的 evidence_level。

SDK 的测试源码/BringUp/实测材料继续遵守仓库忽略规则；发布 CLI 运行器及配置格式不会把私有测试变成公开包的强制依赖。

## 10. 结果、退出码与证据

最终对象遵守 [result.schema.json](schemas/result.schema.json)。状态轨迹按步骤保留，末尾的 target offline 不能擦掉先前写入成功的事实。每个步骤单独含 read/comparison/save/execution/validation 等适用结果；不适用字段不用伪造成功值。

```json
{
  "schema_version": 1,
  "tool_version": "design",
  "run_id": "example-read",
  "command": "verify",
  "status": "failed",
  "exit_code": 30,
  "complete": false,
  "evidence_level": "hardware",
  "states": {"jlink_probe": "online", "target": "online"},
  "steps": [
    {"name": "read", "status": "ok", "details": {"bytes": 4096}},
    {"name": "compare", "status": "not_run", "details": {"comparison": "not_requested"}},
    {"name": "save", "status": "failed", "details": {"save": "failed"}}
  ],
  "artifacts": [],
  "error": {"code": "save_failed", "message": "Local storage write failed", "retryable": false, "details": {}},
  "final_state": {"target_run_state": "running", "probe_released": true, "usb_released": null}
}
```

| 退出码 | 分类 |
|---|---|
| 0 | 请求完成，或仅完成 dry-run 计划；通过 status/evidence_level 区分 |
| 2 | 参数/配置/范围不合法 |
| 10 | 主机依赖/工具不可用 |
| 11 | 探针离线 |
| 12 | 目标调试不可达 |
| 13 | 目标身份不符、保护或前置条件不满足 |
| 14 | 占用、权限或选择器不唯一 |
| 20 | USB 未枚举/所需 CDC 不存在 |
| 21 | SirinLink 无有效握手响应 |
| 22 | 协议不兼容/响应格式错误/能力不支持 |
| 23 | 命令被设备拒绝或返回失败 |
| 24 | 超时/断连/执行结果未知 |
| 30 | 本地保存/证据写入失败 |
| 31 | Flash 内容不一致 |
| 32 | Flash 编程失败/部分写入 |
| 40 | 测试或质量验收失败 |
| 41 | 主机测试套件不可用/未执行任何测试 |
| 50 | 工具内部错误 |
| 130 | 用户中断，已尝试有界收尾 |

稳定的 `error.code` 比退出码更细，例如 tool_missing、probe_busy、target_offline、usb_not_enumerated、cdc_permission_denied、protocol_no_response、command_outcome_unknown。首次主失败决定退出码，收尾和保存的附加失败进入独立步骤/errors；无先前失败时保存失败不能返回 0。重试建议必须基于操作是否可能已经发生，不能仅因 timeout 一律 retryable=true。

运行目录包含 `manifest.json`、`result.json`、`events.jsonl`、backend 原始日志、脚本、镜像段/哈希、原始收发和采样文件。manifest 初始为 in_progress，记录 CLI/SDK 提交及 dirty 状态、配置摘要、输入镜像与编解码器版本、设备观测、UTC 和 monotonic 时间、输出文件哈希及结束原因。原始设备标识与路径只留本地，不写回 SDK 示例文档。

散列清单只覆盖已经关闭的原始数据/脚本/日志等载荷文件，不计算 manifest/result/events 之间的循环自引用哈希；如需封存整个目录，在所有文件关闭后另外产生外部清单。每次复位后的 USB 重枚举都重新匹配设备并握手，不按上次端口路径自动恢复命令；未经设备回读核对的本地 ELF 哈希标记为 provided_only。

烧录、实际运动和写设备命令默认生成运行目录；只读操作默认可仅输出结果，显式 `--save/--output` 才持久化。默认保存根为调用 cwd 的 `build/srneos/<UTC>-<随机后缀>`，保存位置可配置。目录创建及磁盘可写检查必须先于有副作用步骤，运行时磁盘失败则中止后续动作并有界停止，不假装可以继续产生可靠证据。`report RUN_DIR` 离线汇总已有证据；`decode FILE` 离线重放协议，均不碰设备。

## 11. 兼容与边界

现有 Python 工具先保留入口，公共逻辑提取后再改为薄包装；用黄金向量保证 V2/SirinRMT 线格式不变，不同时重写协议和 CLI。现有脚本的自动重连、Ctrl-C 返回码和只发 INVALIDATE 等行为不能直接复制成新合同，需要按本设计分别适配。

不在首版加入任意 RAM 写、自动解锁、任意脚本执行、永久后台 daemon、自动 PID 调参或通用 MCU 插件体系。可在现有边界内逐步增加 build、参数、panic 解码和健康快照入口；任何新增入口都使用相同状态、保存和错误合同。

必要的固件能力扩展另行设计，例如完整 build identity 或通用 Input 接收/使能反馈。当前 HELLO 不能证明镜像匹配，EVENT 不能证明业务执行，主机配置声明不能伪装为设备回读；首版通过明确 unknown/not_evaluated 表达这些边界。
