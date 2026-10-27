# SRNOS 基础层与内存

公共 C 接口使用 `SRNOS_`，C++ 使用 `SRNOS::`。提供字节操作、边界检查、Endian、数值及 Fault 能力；Fault 保留记录采用 v2 布局。

## CRC32

`Core/C/CRC32.h` 提供 CRC-32/ISO-HDLC 一次性和增量接口，初值/最终 XOR 均为 `0xFFFFFFFF`。Update 接收未 finalize 的状态；分块完成后仅最后 XOR 一次。`123456789` 的结果为 `0xCBF43926`。Storage 和 SirinLink 共用实现，CRC 不依赖通信服务或 RTOS。

```c
#include "SirinEmbedOS/Core/C/CRC32.h"
uint32_t crc_example(void) {
    const char bytes[] = "123456789";
    uint32_t state = SRNOS_CRC32Update(SRNOS_CRC32_INIT, bytes, 4);
    state = SRNOS_CRC32Update(state, bytes + 4, 5);
    return state ^ SRNOS_CRC32_FINAL_XOR; // 0xCBF43926
}
```

上述示例不含字符串末尾的 NUL。一次性计算可用 `SRNOS_CRC32(bytes, 9)`；两种写法的结果相同。

## 启动及异常

`SRNOS_BoardEarlyInitialize()` 必须在 main 的 `USER CODE 1`、MPU/cache 开启前执行：开启 D2 SRAM 时钟，清零专用 BSS/DMA/Watch 段，复制 ITCM，初始化 Fault 并启用故障陷阱。ST Arm Clang/Picolibc 的现有 multilib 允许 Normal memory 非对齐访问，因此默认 `SRNOS_TRAP_UNALIGNED=0`；仅在整个运行库与程序均保证严格对齐时开启该审计开关。除零和可配置异常始终启用。生成启动汇编仍负责普通 data/TLS/BSS 和 C++ 构造函数。全局构造不应访问尚未初始化的板级/专用段资源。

六个异常向量由 `Board/Support/System/fault_handlers.c` 的 naked 汇编入口拥有。生成的同名 handler 由构建适配改名，其他中断保持 CubeMX 所有权。入口先读取原 MSP/PSP/LR，再跳转到解码器。Fault 可以解析基本/扩展/惰性 FPU 栈帧，并写入保留记录。异常处理时不调用分配器或 RTOS 锁。

默认 Panic/Fault 在保存 retained 记录后，经 USART1 TX（PB14 AF4，115200、8N1）轮询输出报告，再执行配置的 reset/halt。正常启动不预留或占用 USART1；进入不可恢复的 Fault 后才复位 USART1 并接管 TX，无需应用绑定或提前初始化。实现位于 `Board/Support/System/fault_usart1.c`，使用 HSI、保留 HSI 分频，并对时钟/发送就绪进行有界轮询，不依赖 HAL tick、DMA、RTOS 或动态内存；超时停止串口输出，仍继续 Fault 处理。此时会将 USART1/6、UART9/10 共享内核时钟切到 HSI，不恢复被中断的协议，随后终止或复位。时钟、电源、引脚或 MCU 本身失效时不保证串口可用。

应用仍可在配置阶段调用 `SRNOS_Platform_ReserveEmergencyOutput` 一次覆盖默认输出，系统配置冻结后拒绝注册；自定义回调须在关中断状态下有界返回，不能等待 DMA、RTOS 或动态内存。注册自定义 writer 后不再接管 USART1。正常日志不可调用紧急通路。历史验证见 [CHANGELOG](CHANGELOG.md)。

## 链接布局

固件仅使用项目维护的 `Board/Linker/SirinEmbedOS.ld`。CubeMX 生成脚本不参与最终链接。普通 data/TLS/BSS 位于 RAM_D1，CAN Router 专用 BSS 和 Watch 位于 ITCM，DTCM 只承载显式热数据、heap_5 首区和 MSP。DTCM/D1 heap_5 数组及注册仍只有 `kernel_board.c` 中的一套。

- DTCM/D1 heap 默认 64 KiB/128 KiB，由 SDK 配置头控制；不能当 DMA 分配器使用。
- MSP/libc arena 默认各 8 KiB，可用链接符号 `SRNOS_MSP_BYTES` / `SRNOS_LIBC_BYTES` 覆盖；它们不是 RTOS heap 的别名。
- `.dma_nc`、`.dma_nc_buf`、`.eth_dma` 在非缓存 D2，`.bdma_buf` 在 BDMA 可达的 D3。均为 NOLOAD，启动时显式清零。
- `SRNOS_ITCM_BSS` 将仅 CPU 访问、无非零初值的状态放入 `.itcm_bss`；当前为 CAN Router 上下文。`.debug_watch` 也位于 ITCM，按符号读取。二者与 `.itcm_text` 共享 64 KiB，总容量由链接器检查。
- `.dtcm_bss` 启动清零；ITCM BSS/Watch 使用对齐的 64 位 volatile 写入清零，`.itcm_text` 从对齐 Flash 源按 64 位复制，避免首次写 ITCM ECC 字时读取旧内容。反汇编验证为 STRD；两块 heap payload 不保证清零。
- 专用段初始化发生在 C++ 全局构造之后、SDK 配置之前。构造期注册表和带初值对象不能直接放进 ITCM BSS；不把通用 heap、DMA staging 或 Fault retained 搬入 ITCM。
- `SRNOS_DbgMpuDcache` 包含 `itcm_text_size` / `itcm_bss_size`，记录大小为 72 B，解析时检查 `header.size`。
- `.sirinos_noinit` 在非缓存 D2，启动不清零，Fault 通过 magic/version/size/checksum 判断有效性。掉电后不保证保留。固件升级可能改变地址或记录布局，不保证跨固件保留；同一固件的软件复位保留此段。
- `_sbrk/sbrk` 仅在独立 libc arena 中移动。负增量、溢出和耗尽返回失败。

链接断言检查容量、堆栈相交、各银行上界及 data/TLS 连续复制关系。DMA buffer 不允许依赖 C 静态初始化值。

## 内核诊断与 libc

FreeRTOSConfig 只包含 `Diagnostics/Runtime/trace_internal.h` 这一份 trace 宏，集中向 Fault 元数据和轻量运行统计分发。主动采样由 `Services/SystemMonitor` 的 Low/4 KiB 线程负责，不做周期扫栈。DWT runtime counter 和 Fault 元数据不依赖监测线程，每 tick 维护 DWT 64 位扩展，避免关闭监测后漏掉 CYCCNT 回绕。

Picolibc retarget lock 使用静态递归 FreeRTOS mutex；首次调用无需 libc malloc，动态锁最多 8 个。启动前单线程使用不取锁；调度暂停时只尝试立即取得锁；ISR 调用拒绝。此适配不保证上游未加锁的共享 `FILE` 操作线程安全。不同线程的 errno/TLS 仍由 FreeRTOS Picolibc TLS 管理。

## 验证范围

Core、Fault、解码工具、数学、PID 和纯功率模型提供独立主机脚本；构建集成负责链接布局和向量检查。ST Arm Clang 反汇编须保持 Fault 入口无 C prologue。编译及静态检查不替代板端异常触发、FPU/TLS、缓存一致性、DMA 和 RTOS 调度验证。

## 板级边界

板级早期启动、Fault/libc 实现在 Board/Support/System；Core 公共操作及内存段合同不变。

## 公共依赖

公共基础操作使用 SRNOS 命名；配置头统一为 `SirinEmbedOS/Config.h`，段放置宏、版本宏和自维护配置宏统一 `SRNOS_*`。链接器约定符号、libc/RTOS 固定入口属于明确 ABI 例外。父应用只从 Include 公共路径取得依赖，不包含私有 Config/Board/Kernel 头。
