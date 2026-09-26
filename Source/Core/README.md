# SRNOS 基础层与内存

公共 C 接口使用 `SRNOS_`，C++ 使用 `SRNOS::`。字节、边界、Endian、数值与 Fault 保留原行为，不保留旧名兼容别名。非协议标识符的更名不改变 Fault v2 retained record 布局。

## 启动及异常

`SRNOS_BoardEarlyInitialize()` 必须在 main 的 `USER CODE 1`、MPU/cache 开启前执行：开启 D2 SRAM 时钟，清零专用 BSS/DMA/Watch 段，复制 ITCM，初始化 Fault 并启用故障陷阱。ST Arm Clang/Picolibc 的现有 multilib 允许 Normal memory 非对齐访问，因此默认 `SRNOS_TRAP_UNALIGNED=0`；仅在整个运行库与程序均保证严格对齐时开启该审计开关。除零和可配置异常始终启用。生成启动汇编仍负责普通 data/TLS/BSS 和 C++ 构造函数。全局构造不应访问尚未初始化的板级/专用段资源。

六个异常向量由 `Board/Glue/fault_handlers.c` 的 naked 汇编入口拥有。生成的同名 handler 由构建适配改名，其他中断保持 CubeMX 所有权。入口先读取原 MSP/PSP/LR，再跳转到解码器。Fault 可以解析基本/扩展/惰性 FPU 栈帧，并写入保留记录。异常处理时不调用分配器或 RTOS 锁。

默认紧急输出为空，只保留记录并执行配置的 reset/halt。应用可以在配置阶段调用 `SRNOS_Platform_ReserveEmergencyOutput` 一次，注册已明确独占的传输，系统配置冻结后拒绝注册；回调必须在关中断状态下有界返回，不能使用 DMA 完成等待、RTOS 或动态内存。Fault 不初始化或复位 USART1，也不抢占其他协议端口。正常日志不可调用紧急通路。

## 链接布局

固件仅使用项目维护的 `Board/Linker/SirinEmbedOS.ld`。原生成脚本不参与最终链接，历史 fragment 已整合到这一份脚本。普通 data/TLS/BSS 位于 RAM_D1，DTCM 只承载显式热数据、heap_5 首区和 MSP。DTCM/D1 heap_5 数组及注册仍只有 `kernel_board.c` 中的一套。

- DTCM/D1 heap 默认 64 KiB/128 KiB，由 SDK 配置头控制；不能当 DMA 分配器使用。
- MSP/libc arena 默认各 8 KiB，可用链接符号 `SRNOS_MSP_BYTES` / `SRNOS_LIBC_BYTES` 覆盖；它们不是 RTOS heap 的别名。
- `.dma_nc`、`.dma_nc_buf`、`.eth_dma`、`.debug_watch` 在非缓存 D2，`.bdma_buf` 在 BDMA 可达的 D3。均为 NOLOAD，启动时显式清零。
- `.dtcm_bss` 启动清零，`.itcm_text` 从 Flash 复制；两块 heap payload 不保证清零。
- `.sirinos_noinit` 在非缓存 D2，启动不清零，Fault 通过 magic/version/size/checksum 判断有效性。掉电后不保证保留。
- `_sbrk/sbrk` 仅在独立 libc arena 中移动，不再比较 DTCM `_estack` 与 D1 地址。负增量、溢出和耗尽返回失败。

链接断言检查容量、堆栈相交、各银行上界及 data/TLS 连续复制关系。DMA buffer 不允许依赖 C 静态初始化值。

## 内核诊断与 libc

FreeRTOSConfig 只包含 `Diagnostics/Runtime/Trace.h` 这一份 trace 宏，集中向 Fault 元数据和 HSKP 分发。`SRNOS_DiagnosticsPrepare()` 在时钟设置后、kernel initialize 前调用，`SRNOS_DiagnosticsStart()` 在 kernel ready 后、scheduler start 前创建可选采样线程。DWT runtime counter 和 Fault 元数据不依赖采样线程。每个 tick 维护 DWT 64 位扩展，避免关闭采样后漏掉 CYCCNT 回绕。

Picolibc retarget lock 使用静态递归 FreeRTOS mutex；首次调用无需 libc malloc，动态锁最多 8 个。启动前单线程使用不取锁；调度暂停时只尝试立即取得锁；ISR 调用拒绝。此适配不保证上游未加锁的共享 `FILE` 操作线程安全。不同线程的 errno/TLS 仍由 FreeRTOS Picolibc TLS 管理。

## 验证范围

Core、Fault、解码工具、数学、PID 和纯功率模型提供独立主机脚本；构建集成负责链接布局和向量检查。ST Arm Clang 反汇编须保持 Fault 入口无 C prologue。编译及静态检查不替代板端异常触发、FPU/TLS、缓存一致性、DMA 和 RTOS 调度验证。
