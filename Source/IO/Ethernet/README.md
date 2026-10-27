# 原始以太网二层接口

固定 STM32H723 MAC + LAN8720A RMII 通路。公开入口 `SirinEmbedOS/IO/Ethernet/Ethernet.h`；应用在 `SRNOS_ApplicationConfigure` 设置 `config->ethernet.enabled = true`。默认关闭，配置启动冻结，不支持运行期换 MAC/重绑硬件。零 MAC 从 MCU UID 派生本地管理单播地址；也可显式指定合法单播地址。UID 哈希不能保证跨设备绝对无冲突，部署时可由应用分配地址。

`GetStatus` 给出 PHY ID、地址、BCR/BMSR/模式/协商原始寄存器、链路速率/双工、RCC 推算的 MCO 频率、链路 epoch 和收发/恢复计数。`Send` 复制完整 Ethernet 帧；成功仅代表入队。`Receive` 为单消费者非阻塞复制。两者仅任务上下文，调用栈额外预留约 1.6 KiB。应用不持有 DMA 指针，不直接访问 HAL。接口保留二层 padding，去掉 FCS；发送 padding/FCS 由 MAC 插入。1518 B 是含单层 VLAN 的缓存上限；普通无 VLAN 帧上限为 1514 B。本二层模块不处理 IP/ARP/UDP 或组播订阅；静态 IPv4/ARP/UDP/Ping 由 `Source/Services/Network` 提供，TCP 不启用。

## 所有权与调度

- `SRNOS_Ethernet` 由 Kernel 建立，优先级 16 / 栈 4096 B。低于默认应用优先级 24 和 CAN/IMU/串口实时 worker，启动 HAL/MDIO 失败等待不阻塞它们。ETH IRQ 优先级 7，仅调用 HAL 并通知 worker。
- PHY 由单个 worker 持有，扫描 0..31 地址且要求恰好一个 LAN8720A，启动自动协商；每 250 ms 双读 BMSR 处理 latch-low，再读取寄存器 31 的实际协商结果。nINT 没接 MCU，使用轮询。
- 正常接收/完成以中断唤醒；无活动最长等待 250 ms，TX 在途最多等待 10 ms。每轮 RX 最多八帧，饱和后让出一 tick。高负载允许有界丢包，不承诺线速。
- TX/RX 静态复制队列各四帧，D1 约 12 KiB；RX 满丢新帧。TX 多生产者，满返回 FULL。TX/RX 超过 100 ms 或链路 epoch 改变均不再交付/发送。RX 的 `rx_dropped` 统计队列满/恢复清队列，消费端过期丢弃不计入它。
- 单帧 TX DMA：提交后保留唯一 buffer，直到描述符归还且 HAL release callback 完成。100 ms 未完成或 fatal DMA 错误先 RCC reset 停止 DMA，再复用缓存。链路断开也递增 epoch 并清队列，1 s 后重新初始化；不重播旧包。
- HAL 原有四个 RX/TX 描述符留在 `.dma_nc`；五个 1536 B RX buffer 和一个 1536 B TX buffer 共 9216 B 在 `.eth_dma` D2 非缓存区。HAL ReadData 在返还帧前补 descriptor，因此 RX 比描述符多一个 buffer。NOLOAD 早期清零和每次初始化显式复位；控制结构/复制队列在 D1。
- 静态 D1 队列及 D2 DMA 缓冲在构建中固定预留，运行期开关不回收这些区域。启用时启动 heap 预算增加 5 KiB；应用应在总堆预算内保留该容量。

## 固定板接入

`Board/Support/IO/ethernet_board.c` 是唯一运行期 MAC/HAL/IRQ owner，复用 CubeMX 的 MSP 引脚和描述符，直接受控调用 HAL_ETH_Init，避免生成的 `MX_ETH_Init` 默认 MAC 和 Error_Handler。生成函数仍保留但不调用，CubeMX 生成区不修改。

PA8 MCO1 = PLL1Q / 11 = 50 MHz，同网送至 PA1 ETH_REF_CLK 和 LAN8720A XTAL1/CLKIN。PE14 低有效复位：检查 RCC 配置后保持低 2 ms，释放后等 50 ms 才访问 MAC/PHY。nINTSEL 使用内部上拉；外接连接器不得在复位锁存时拉低它。其余 RMII 引脚沿用 IOC。RCC 推算/链路成功不能替代示波器对幅度、边沿、占空比的检查。

`last_error`：1 初始化/身份失败，2 MDIO 链路查询失败，3 MAC 启动失败，4 TX 超时，5 TX 提交失败；高位为 1 时低位保存 fatal DMA 状态。硬件故障是模块 ERROR/重试，不导致整个系统 Panic。

## 验证

历史实机范围见 [CHANGELOG](CHANGELOG.md)；主机注入不代表拔线、电气或持续高负载验证。

## 静态 IPv4 接入

启用 Network 后，由其独占原始 Ethernet 收发；应用改用 UDP 接口，原始 Send/Receive 返回 `SRNOS_ETHERNET_OWNED`，状态查询仍可用。配置与资源合同见 [Network](../../Services/Network/README.md)。

## 原始帧读取示例

下例只启用二层通路；在现有配置函数中合并相应字段，不要定义第二个 ApplicationConfigure。缓冲由唯一消费者持有，示例不回显、不发送控制指令。

```c
#include "SirinEmbedOS/Kernel/System.h"
#include "SirinEmbedOS/IO/Ethernet/Ethernet.h"
void configure_raw_ethernet(SRNOS_SystemConfig *config) {
    config->ethernet.enabled = true;
    config->network.enabled = false;
}
SRNOS_EthernetResult receive_frame(SRNOS_EthernetFrame *frame) {
    return SRNOS_EthernetReceive(frame); // 任务中调用，EMPTY 表示当前无帧
}
```

调用者只在 `OK` 时读取 `frame->length` 与数据。若需要 IP/UDP，启用 Network 并改用其接口，不能让两个消费者同时读取二层队列。
