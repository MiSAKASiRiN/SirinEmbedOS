# 验证记录

本文保存对应归档的原始报告。日期、版本、通过范围和结束状态仅对当时现场有效，不表示当前板上状态。当前阅读方法见本目录说明文件。

---

<!-- 原报告：README.md；原文 SHA256：ca1fbaf10240f58c38174930425e415228778e2657e9536503bb4a503d4c09ab -->

# Native CAN RX 目标栈审计（2026-10-03）

使用 `build/can-fd-20261003/{Debug,Release}/compile_commands.json` 中的 ST Arm Clang 编译命令，加入 `-fstack-usage` 独立编译源文件。`compile_commands.json` 保存本次独立命令；`.su` 保存编译器的每函数静态栈，`.asm` 为 `starm-objdump -dr -C` 的序言、调用和退栈片段，省略不相关算术指令；不保留对象文件。`before` 是改为 4 帧 pop 前同一目标配置生成的对象证据。

| 回调入口路径 | Debug 改前 | Debug 改后 | Release 改前 | Release 改后 |
|---|---:|---:|---:|---:|
| Kernel trampoline | 96 | 96 | 96 | 96 |
| Native CAN RX worker | 3536 | 512 | 3528 | 504 |
| KernelRunLoop | 64 | 64 | 64 | 64 |
| C++ step invoke（含 inline route 和 FD frame） | 112 | 120 | 112 | 120 |
| CANFDCoreOnRxFrame | 120 | 120 | 112 | 112 |
| 合计（字节，应用回调入口前） | 3928 | 912 | 3912 | 896 |

`system_thread_entry` 的 8 字节帧在 `bx entry` 前已经释放，不与 worker 相加；`.asm` 保存这个 tail call。`trampoline` 和 `KernelRunLoop` 通过保留其栈帧的 `blx` 调用。改后 callback 入口链减少 3016 字节；未调大原有 4096 字节栈，未增加静态接收缓存。

这不是线程完整最坏栈上界：应用回调及其调用链、额外 TLS、启动/等待等其他路径应单独核算。当前默认 Debug/Release ELF 的 `__tls_size=0`、`__tls_align=4` 见 `tls-symbols.txt`，父项目可能增加 TLS。固定 FreeRTOS CM4F port 的 PendSV 向 PSP 保存 R4–R11/LR（36 字节），使用 FP 时另存 S16–S31（64 字节）；加 Cortex-M 异常基本帧（32 字节）及可选 FP 扩展帧（72 字节），上下文预算为 68 或 204 字节，另考虑对齐。最大 FP 上下文下，改前入口链已经超过 4 KiB；改后静态入口链加 204 字节为 Debug 1116、Release 1100 字节，尚未计应用回调/TLS。板端栈水位和压力测试仍待 PCB 在线。

行为测试：`Tests/IO/CAN/Native/run_host_tests.sh` 与 `Tests/IO/CAN/Router/run_host_tests.sh` 通过。真实 Native worker 在 70 帧 backlog 下每次 pop 至多 4 帧，调度仍为 32/32/6，满轮继续 notify + yield；Classic wait-race 额外一帧按第四轮处理，FD 64 字节不截断。
