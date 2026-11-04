# CmdLineTool 变更记录

## Unreleased — 内置遥测固定周期（2026-11-05）

- CLI 复用既有传感器协议周期表，订阅/退订使用各流固定周期；修复默认 `telemetry read --streams system,ins` 被设备以 INVALID_ARGUMENT 拒绝的问题。`--hz` 继续用于 SOURCE；固件 API、采样频率及控制行为不变。
- 主机假客户端覆盖全部内置流订阅/退订周期，实板默认 system/ins 订阅、解码及退订通过，USB 只读验证单独记录于父项目；SDK 不携带原始现场数据。


## Unreleased — CLI 实现（2026-11-04）

- 实现可安装 Python 包与 `srneos`/`python -m srneos`，提供 connect、flash、verify、read、log、telemetry、command、parameters、rmt、test host、run/cancel、doctor、report/decode。父项目通过配置和公共协议调用，未改固件代码。
- J-Link 使用隔离的 PyLink/SEGGER 动态库子进程及超时，不依赖终端文字解析；同时支持生成固定 Commander 读取脚本。实施芯片/UID/Flash 大小核对、扇区保留策略、备份和全编程后独立回读。DLL 关闭后的运行状态明确为未知，保留关闭前观测。
- CDC0 日志与 CDC1 握手/遥测/参数/命令分离；复用 SDK 编解码器，单 RX 会话、有界异步写盘、无副作用命令重发。RMT 有界零输入预热、一次 ENABLE、同会话零目标/DISABLE/INVALIDATE 收尾和协作 cancel。
- 增加当前安装/使用文档、配置/地址读取/联测/主机测试示例；原始设计保留未实现的扩展。Python 构建产物与本地测试保持 Git 忽略。
- 主机验证：42 项 CLI/协议/假探针/参数/证据/停止用例通过，其中 PTY 使用真实 pyserial 字节流，替代不受 PTY 支持的 DTR ioctl；SDK 原有 SirinLink Codec 与 Input 两套主机回归通过，原三项脚本帮助入口保持可用。JSON Schema、TOML 和文档链接检查通过。
- wheel 在独立虚拟环境安装后，从 SDK 外的工作目录验证 doctor/capabilities、地址清单与联测计划 dry-run、全部共享 codec 导入；不依赖源码目录。当前本地可用环境为 SDK `build/srneos-venv`，wheel 位于 `build/srneos-dist/`。
- 未在真实 J-Link/USB 板卡上验收，未烧录、写设备参数或运动。Windows/Linux 硬件行为未验证；SOURCE V1 回退、统一 CSV、跨通路计划和业务反馈断言尚未实现。EVENT 的执行确认仍需应用证据。

## 2026-11-04 初始设计

- 2026-11-04：建立 `srneos` CLI 设计，覆盖 J-Link/目标分层状态、烧录与 Flash 回读、结构化地址读取、本地保存、CDC0 日志、CDC1 SirinLink 遥测/命令、SirinRMT 有限时测试及主机测试运行器。
- 明确 SDK 持有通用实现，父项目仅提供配置、镜像、业务映射与反馈判据；增加统一结果 Schema、TOML 配置示例、会话所有权、失败/停止语义和分阶段验收清单。
- 设计阶段仅交付设计与格式文件，当时未实现 CLI，也未连接硬件、烧录或执行运动。固件公共 API、生成边界、调度及内存布局不变。
- 设计文件检查通过：JSON Schema Draft 2020-12 自校验、2 份有效结果与 5 份无效结果判定、TOML 解析、17 个本地文档链接及空白检查；校验依赖仅安装于忽略的本地 build 虚拟环境，不是 CLI 安装或功能验收。
