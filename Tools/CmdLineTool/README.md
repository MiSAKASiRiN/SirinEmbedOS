# srneos — SirinEmbedOS CLI

`srneos` 是 SDK 持有的 Python 主机工具，统一 J-Link 连接/烧录/回读、CDC0 日志、CDC1 SirinLink 遥测/命令、SirinRMT 输入测试和主机测试运行。父项目只安装工具、配置固件及设备选择器、注册业务命令/遥测并提供运动限制，不复制协议和调试脚本。

当前为 0.1.0。已实现命令及主机验证范围见下文；真实 J-Link/USB 板卡、烧录和运动尚未在本轮验收。支持的目标限定为当前 SDK 的 STM32H723VG（DEV_ID 家族 0x483、1024 KiB Flash）。不把探针自身的 MCU 当成目标身份。

## 安装

Python 3.11+。在 SDK 根目录创建独立环境：

```sh
python3 -m venv build/srneos-venv
build/srneos-venv/bin/python -m pip install -e Tools/CmdLineTool
source build/srneos-venv/bin/activate
srneos --version
srneos doctor --json
srneos capabilities --json
```

Windows 使用 `build\srneos-venv\Scripts\python.exe` 和 `Activate.ps1`。CLI 本身不依赖当前工作目录；配置中的路径相对配置文件，命令行路径相对调用目录。父项目安装其 SDK 子模块即可：

```sh
python3 -m venv build/srneos-venv
build/srneos-venv/bin/python -m pip install -e SirinEmbedOS/Tools/CmdLineTool
source build/srneos-venv/bin/activate
srneos --help
```

依赖 pyserial、pylink-square、pyelftools。J-Link 另需安装 SEGGER Software Pack；不随 wheel 分发。PyLink 可自动查找动态库，也可用 `--jlink-library PATH` 或配置的 `jlink.library` 指定。`doctor` 检查主机依赖，不连接探针/目标。

需要分发时，从**完整 SDK checkout** 构建 wheel；构建时复用相邻 `Tools/SirinLink` 的源码，wheel 本身包含这些模块，可独立安装：

```sh
python -m pip install build
python -m build --wheel --outdir build/srneos-dist Tools/CmdLineTool
```

不单独复制 `Tools/CmdLineTool` 源码目录构建；它需要相邻的 SDK 编解码器。发布产物为 wheel，不以缺少相邻源码的独立 sdist 交付。

## 公共规则

- `--json`：stdout 一份最终 JSON，进度和 run_id 在 stderr。`--jsonl`：日志/遥测输出 sample 行，最后输出 result 行；其它命令仅有最终 result 行。
- `--config FILE`：显式 TOML；参数优先于配置。未知配置字段拒绝执行，示例见 [srneos.example.toml](examples/srneos.example.toml)。
- `--dry-run`：离线校验和计划，不枚举/打开设备。可以显式导出读取脚本；结果是 planned，不是硬件成功。
- `--output DIR`：创建全新证据目录，拒绝覆盖。烧录、命令、RMT、run、遥测 capture、主机测试 run 默认创建 `build/srneos/<run-id>`。
- `--save FILE`：保存本次主要读取结果到本地，绝不触发设备 EEPROM SAVE。既有文件不会覆盖。父目录必须存在。
- `--timeout SECONDS`：默认 10 s。J-Link 为整次 worker 的期限；USB 为每次请求期限；主机测试为每个用例默认期限。烧录通常应显式设置 120 s；长命令按实际业务期限指定。
- `--duration SECONDS`：观察/输入发送时长。首版要求有限正数，无无限采集。RMT 动作最多 60 s，另有预热与收尾。

所有例子中的 `SERIAL`、`CDC0_PATH`、`CDC1_PATH`、镜像和业务字段都须换为实际值。复制配置后先调整父项目运动范围；示例限速不是所有机器人的适用值。

## J-Link 与目标连接

```sh
# 检查探针；未指定 --probe 时只允许唯一候选
srneos connect jlink --probe SERIAL --json
# 显式请求目标 SWD 连接、芯片身份与 UID
srneos connect jlink --probe SERIAL --target --json
```

结果在 `states.jlink_probe` 和 `states.target` 分开记录：

| 结果 | 含义 |
|---|---|
| probe=offline，target=not_checked | 所选探针不在线 |
| probe=online，target=offline | 探针可访问，但本次目标调试连接失败 |
| probe=online，target=online | 目标调试连接和身份读取成功 |
| enumerated/unknown/busy/permission_denied/mismatch | 访问、占用、身份或证据问题，不能概括成设备离线 |

底层使用 PyLink/SEGGER 动态库，调用放在独立 worker 进程，父进程设置硬期限。关闭 GUI、自动固件升级和隐式 connect-under-reset 重试；拒绝解锁对话框。库缺失/版本不支持返回主机错误，不自动降级、擦除或重置。

每次读/写都重新核对目标；`--expected-uid` 可绑定预期 UID，格式是按地址递增的三个 u32 十六进制字拼接，不含空格也可。连接成功不表示固件业务正常。结束前记录运行/暂停状态，关闭 DLL 后无法再次观测，故最终 `target_run_state=unknown`，可查看 `before_disconnect` 的最后实测状态；`probe_released=true` 只表示库已关闭。

## 烧录与 Flash 验证

```sh
srneos flash --probe SERIAL --image firmware.elf --erase-policy preserve \
  --after halt --timeout 120 --output build/srneos/flash-01

srneos flash --probe SERIAL --image firmware.bin --address 0x08000000 \
  --erase-policy preserve --after run --timeout 120

srneos verify --probe SERIAL --image firmware.elf --timeout 60
srneos verify --probe SERIAL --address 0x08000000 --length 4096 --save flash.bin
srneos verify --probe SERIAL --address 0x08000000 --length 4096 --expected-sha256 HEX_SHA256
```

支持 ELF/AXF、Intel HEX、BIN。BIN 必须给地址；ELF 按 PT_LOAD 的物理加载地址读取实际文件字节，跳过 BSS/NOLOAD。目标 Flash 范围为 `0x08000000..0x080FFFFF`，扇区为 128 KiB。越界、重叠和无效 HEX 在打开设备前失败。

`flash` 必须明确 `--after run|halt`，可由配置给出。擦除策略：

- `reject`（默认）：镜像未完整覆盖某擦除扇区则拒绝。
- `preserve`：先读受影响扇区，合并镜像，编程整扇区，保留段外数据。
- `allow`：段外字节明确填 `0xFF`，允许擦除这些数据。

所有编程完成后，**独立回读所有受影响扇区逐字节比较**，包括 preserve 保留区。全部通过后才复位，并进入指定 run/halt 状态；失败不自动启动新镜像。`--backup NEW_DIR` 在编程前将受影响扇区备份到全新目录，备份失败不写设备。超时/中断可能留下部分编程结果，不自动重烧或回滚。

`verify` 不要求镜像：仅给地址和长度时就是 Flash 内容读取，返回哈希并可保存，`comparison=not_requested`；给镜像/预期哈希才做比对。多段镜像使用 `--output` 存分段 bin，不支持用一个 `--save` 偷填空洞。

## 地址读取与 Commander 脚本

```sh
srneos read --probe SERIAL --address 0x1FF1E800 --type u32 --count 3 --save uid.json
srneos read --probe SERIAL --address 0x24000000 --type u32 --count 16 --save ram.bin
srneos read --probe SERIAL --request-file reads.toml --output build/srneos/read-01
srneos read --address 0x24000000 --type u32 --count 16 --dry-run --script read.jlink
```

读取清单见 [reads.example.toml](examples/reads.example.toml)。支持 u8/u16/u32/u64/i8/i16/i32/i64/f32/f64/bytes，little endian，检查对齐和 16 MiB 单项上限。MMIO/未知范围须 `--allow-mmio`；外设读取可能清除状态。运行中 RAM 为非原子观测；显式 `--halt` 暂停读取，结束时仅恢复由本次操作暂停的运行目标。64 位值由两个 32 位总线读取组成，不承诺原子性。

`--save *.bin` 保存原始字节；其它后缀保存地址、类型、值和 raw_hex 的 JSON。终端最多预览 256 项，文件保存完整数据。非有限浮点以结构化 class/null 表达，大整数用十进制字符串保精度。

`--script` 生成只包含固定策略、connect 和 mem8/16/32 的 Commander 命令文件；不会执行它。它不包含 Python 后端的芯片/UID比对和结果归档，手工执行须自己核对目标，例如：

```sh
JLinkExe -device STM32H723VG -if SWD -speed 1000 -autoconnect 0 -NoGui 1 \
  -SelectEmuBySN SERIAL -CommanderScript read.jlink
```

不接受任意脚本作为只读输入。正常 `srneos read` 的实际读取使用动态库接口，不依赖 Commander 文本解析。

## USB 连接与 CDC0 日志

```sh
srneos connect usb --list --json
srneos connect usb --usb-serial USB_SERIAL --json
srneos connect usb --cdc1 CDC1_PATH --handshake --json
srneos log --cdc0 CDC0_PATH --duration 30 --save cdc0.log
srneos log --cdc0 CDC0_PATH --duration 30 --jsonl
```

枚举与打开分开。macOS 使用 IOKit 清单，Linux 使用 sysfs，Windows 使用 PnP 清单，再结合 pyserial 接口信息识别 CDC；多候选不自动选第一个。接口标签不足时要求显式路径。显式路径可能是 PTY/普通串口，因此不能凭成功打开就声称 USB 已枚举；此时 USB 证据可为 unknown。

- USB 清单确认没有设备：`usb=not_enumerated`。
- USB 已有但没有所需串口：`usb=enumerated`，CDC missing/unknown。
- CDC1 可打开但 HELLO 超时：`sirinlink=no_response`；坏帧/版本不匹配独立报告。
- 匹配的 V2 HELLO 成功：`sirinlink=ready`。这不证明全部服务存在或应用已使能。

CDC0 是文本日志，没有握手，安静窗口是 quiet；只有指定 `--require-data` 才因无日志而失败。保存保留原始字节，显示文本时允许替换不可解码字符。CDC1 不作为普通日志口读取。

## CDC1 遥测

```sh
srneos telemetry list --cdc1 CDC1_PATH --json
srneos telemetry read --cdc1 CDC1_PATH --streams system,ins --timeout 5 --json
srneos telemetry capture --cdc1 CDC1_PATH --streams bmi,lsm,ins --hz 1000 \
  --duration 60 --output build/srneos/imu-01
srneos telemetry capture --cdc1 CDC1_PATH --sources app.speed --hz 50 \
  --duration 10 --save speed.jsonl
```

内置流：tasks/system/cycles/bmi/lsm/ins/mag/pressure。SystemMonitor 订阅周期固定 1 s；传感器流按 `--hz` 请求发送周期，不修改传感器 ODR。SOURCE 使用 V2 目录和订阅（5..1000 Hz、5 Hz 步长且不超过 maxHz），当前没有 V1 回退。

`list` 枚举 SOURCE 目录并列出主机已知专用流，未订阅流的固件支持状态标 unknown。`read` 等到每个选择项都有一条接收记录，`capture` 按时长保存。SOURCE invalid 值不算可用样本；内置流保留自身状态/有效位，用户仍需按字段判定业务健康，不能将有包等同于传感器健康。

保存原始 RX/TX、带主机 monotonic 时间及偏移的 I/O 索引、解码后的 samples.jsonl、计数和缺失主题。JSONL 的内置流记录保留批次及每个样本。64 位设备时间不转换为浮点，不补帧或拼接重连。当前统一 CLI 导出 JSONL；逐样本 CSV 仍使用原 [imu_capture.py](../SirinLink/README.md)。

一个端口只有一个 RX 分发器；后台有界队列写盘，写失败/队列满终止采集。结束取消本次成功订阅；失败后仍保存已收到数据。重复 SOURCE 序号不算新样本，最新值流的序号间隔本身不作为丢包判据。

## 命令与业务参数

```sh
srneos command exec --cdc1 CDC1_PATH --id 0x0100 --payload-hex "" --json
srneos command exec --config srneos.toml --cdc1 CDC1_PATH --name app.read_version
srneos command control --cdc1 CDC1_PATH --channel 1 --generation 1 \
  --deadline-us 50000 --payload-file command.bin
srneos parameters list --cdc1 CDC1_PATH --json
srneos parameters read --cdc1 CDC1_PATH --stable-id 0x1234 --save gain.json
srneos parameters write --cdc1 CDC1_PATH --stable-id 0x1234 --value 1.5
srneos parameters persist --cdc1 CDC1_PATH
srneos parameters restore --cdc1 CDC1_PATH
srneos parameters state --cdc1 CDC1_PATH
```

EXEC 等待最终响应，ACCEPTED 不算完成；无自动重发，超时可能已经执行，返回 outcome unknown。参数从本次 SCHEMA/STABLE_IDS 确认身份、类型和范围；WRITE 只改 RAM，persist 才设备 SAVE，restore 才设备 LOAD。STATE 当前保留原始正文，不伪造高层解码。

CONTROL 和 SirinRMT 为 EVENT，发送成功只报告 sent，没有执行 ACK。EXEC 命令号、控制邮箱和业务参数必须由父项目注册；CLI 不提供任意 RAM 写和系统资源热配置。

## SirinRMT 与单会话联测

```sh
srneos rmt enable --cdc1 CDC1_PATH --duration 1 --hz 50
srneos rmt move --config srneos.toml --cdc1 CDC1_PATH \
  --vx 0.1 --vy 0 --wz 0 --duration 0.8 --hz 50 --enable
srneos rmt channels --cdc1 CDC1_PATH --values 0,0,0,0,0 --duration 1
srneos run --config srneos.toml --cdc1 CDC1_PATH --plan motion.toml
srneos cancel --run-id RUN_ID
```

`rmt enable` 为有限时零输入使能测试；`move --enable` 在同一连接中先发送零目标、等待 qualification_ms、仅发一次 ENABLE，再发送目标。vx 前向、vy 左向，单位 m/s；wz 逆时针，单位 rad/s。非零 move 要求配置正的三轴上限且不能超过它。channels 使用百分比，通道映射由父项目决定。

正常结束、Ctrl-C、SIGTERM、cancel 或可处理失败会在**原 RMT session** 内尝试：同类型零目标 → DISABLE → stop_settle_ms 零目标窗口 → INVALIDATE。停止发送失败记录 unconfirmed；失联/进程强杀不能靠主机保证停机，需要应用输入过期撤权。

不能分三个独立进程 enable/move/disable 来延续会话。`run` 计划见 [motion.example.toml](examples/motion.example.toml)，支持有界 enable/move/channels/wait/exec 顺序及同会话遥测。最多 100 步、总步骤期限 60 s、最多一次 ENABLE，禁止使能后切换输入类型或执行阻塞 EXEC。wait 发送当前类型的零输入。协议 ACK 不存在，因此即使计划完成，运动 `validation` 仍为 not_evaluated；采样证据供父项目验收。

`cancel` 使用本机 run_id 对应的持锁进程，要求它自行收尾，不抢串口或按 PID 杀进程。run_id 在启动时打印到 stderr。首版 run 不编排烧录/USB重枚举，不执行任意 Python/shell，也未实现业务反馈断言语言。

## 主机测试、离线解码和报告

```sh
srneos test host list --suite-root SirinEmbedOS/Tests --json
srneos test host run --suite-root SirinEmbedOS/Tests --suite Services/Input \
  --timeout 120 --output build/srneos/host-01
srneos test host run --manifest host-tests.toml --fail-fast
srneos report build/srneos/host-01 --json
srneos decode build/srneos/imu-01/cdc1_rx.bin --json
```

SDK 私有 Tests 不随公开源码分发，缺失返回 suite_unavailable，零用例返回 no_tests，不报 PASS。list 只列举；run 必须显式选择 suite 或 manifest，不自动执行所有发现的脚本。[清单示例](examples/host-tests.example.toml)使用 argv 数组、cwd 和期限，不经过 shell 拼接。清单是用户信任的本地代码，运行器不是硬件隔离沙箱。

每个用例 stdout/stderr 分开保存；非零/超时汇总失败，fail-fast 的未执行项记 not_run。超时结束子进程组。host 通过与板端验证分开标注。decode 只检验已有 COBS/V2 捕获并给出帧预览；report 读取已有结果，不操作设备。

## 输出与失败判读

[结果 Schema](schemas/result.schema.json)定义最终 JSON。`status=planned/ok/failed/interrupted`、`evidence_level=none/host/hardware`、分层 states、逐步 details、error.code 和退出码共同解释结果。

| 退出码 | 含义 |
|---|---|
| 0 / 2 | 完成或 dry-run / 参数或输入错误 |
| 10 / 11 / 12 / 13 / 14 | 依赖不可用 / 探针离线 / 目标不可达 / 前置条件或身份不符 / 占用或选择问题 |
| 20 / 21 / 22 / 23 / 24 | USB/CDC 不可用 / 无握手 / 协议错误 / 设备拒绝 / 超时或未知执行结果 |
| 30 / 31 / 32 | 本地保存失败 / 回读不一致 / 编程失败 |
| 40 / 41 / 50 / 130 | 测试或采集失败 / 套件缺失或零用例 / 工具内部错误 / 中断 |

证据目录包括不可变初始 manifest、终态 manifest-final/result、invocation、原始日志/捕获及哈希。中断/强杀留下的 `.partial` 和 in_progress 不算完整采集。文件使用同目录临时文件和 no-clobber 硬链接发布，文件系统不支持此操作时明确失败，不回退到覆盖写。

完整保存不等于测试通过；读取成功、Flash 比较一致、本地保存成功分别记录。提供的 ELF 哈希不代表 HELLO 已确认同一板上镜像；现有 HELLO 不含完整 build identity。真实固件身份需要 Flash 回读证据。

## 验证与后续范围

[验收清单](ACCEPTANCE.md)保留主机/实板分层要求，[CHANGELOG](CHANGELOG.md)记录实际检查结果。当前完成 Python 包、wheel 和主机自动化验证；J-Link native 调用通过假后端验证，串口字节流通过 PTY 验证（PTY 不具备 USB DTR，测试替代该 ioctl），不等于实板验证。Windows/Linux 适配代码尚未在对应主机硬件上验收。

本地私有测试目录存在时，在 SDK 根目录、激活的开发环境中复测：

```sh
python -m pip install -e 'Tools/CmdLineTool[test]'
python -m unittest discover -s Tests/Tools/CmdLineTool -v
srneos test host run --suite-root Tests --suite Tools/CmdLineTool --timeout 120
```

该测试目录保持 Git 忽略，不包含在 wheel 或公开源码中；公开安装没有此目录时不假装执行了自测。当前实现的 42 项主机用例和原 Input/SirinLink Codec 回归均通过，具体结果及实板边界见变更记录。

[原始设计](DESIGN.md)保留扩展方向，使用方法以本 README 为准。未实现项包括 SOURCE V1 回退、统一 CSV 导出、Flash+USB 跨通路计划和业务反馈断言；不提供模拟成功的占位命令。

供应商接口依据：[SEGGER Command Strings](https://kb.segger.com/J-Link_Command_Strings)、[Commander](https://kb.segger.com/J-Link_Commander)、[PyLink API](https://pylink.readthedocs.io/en/latest/pylink.html)。SDK 线协议依据 [SirinLink](../../Docs/SirinLink.md) 和 [Input](../../Source/Services/Input/README.md)。
