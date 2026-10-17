# MCP2518FD 设备驱动

寄存器/协议实现保留原源码许可，SPI 事务统一经 SDK SPI 通路。私有 `SRNOS_MCP2518FDAttachSPI` 显式声明 bus/CS；未绑定的默认设备通过 Board/Support/IO/can_board 查询，与外置 CAN 初始化共享同一份硬件配置。无配置的设备返回错误，不尝试其他引脚。驱动不包含 router 私有上下文；CAN 路由与调度仍属于 Source/IO/CAN。

主机测试使用实际板级配置实现配合 HAL/SPI 桩，覆盖默认、无效 ID、显式 Attach 及覆盖默认绑定；不证明实际 CAN 总线运行。

自维护 SPI 适配实现为 `SPIAdapter.c`，容量和超时宏使用 `SRNOS_MCP2518FD_*`；`DRV_SPI_TransferData` 仅作为 Microchip 驱动规定的私有适配入口保留。原驱动 `.c` 与三个寄存器/API 头按文件单独记录供应商例外，不将整个设备目录视为供应商代码。

## CAN4 启动配置

外置 CAN backend 按已经冻结的 `SRNOS_SystemConfig.can[3]` 初始化；冻结前不访问 SPI，不允许运行期切换。默认 Classic 1 Mbit/s，Classic 仲裁支持 125/250/500 kbit/s、1 Mbit/s。FD 由应用显式指定同一组仲裁速率、数据速率和 BRS：BRS 开启时数据支持 500 kbit/s、1/2/4/5/8 Mbit/s 且不低于仲裁；关闭时数据必须等于仲裁。MCP 时钟为 40 MHz，SPI 为 10 MHz，自维护适配组合供应商现有 nominal/data timing。

配置前请求并确认 Configuration mode，全部配置完成后请求并确认 Classic mode 6 或 FD Normal mode 0。每次最多 20 次 OPMOD 读取，未匹配时相隔一个 tick；SPI 错误或超限即失败，不发布 worker 句柄。该限制是读取次数，SPI 事务耗时仍计入总时间。125/250 kbit/s 无 BRS 时，data 寄存器使用安全的 1 Mbit/s 占位且 BRSDIS=1，实际帧全程按仲裁速率运行；供应商数据时序实现在 500 kbit/s 关闭 TDC、在 ≥1 Mbit/s 使用自动 TDC，其物理效果待板测。

| 模式 | RX FIFO1 | TX FIFO2 | Message RAM |
|---|---|---|---|
| Classic | 32 × 8 B payload | 32 × 8 B payload | 1024 B |
| FD | 16 × 64 B payload | 12 × 64 B payload | 2016 B / 2048 B |

上表含每项 8 B 头；TXQ、TEF 和 RX timestamp 均关闭并检查，不能额外开启后继续沿用此 RAM 预算。精确 DLC 与 64 B 路径由 IO/CAN backend 管理，FD 长度仅允许 0..8/12/16/20/24/32/48/64 B。RX 类型与冻结模式不匹配即拒绝；RX 保留实际 EXT/BRS/ESI，TX BRS 固定采用配置且 ESI 不可写。FD 计数保留，8 B Classic payload 日志跳过 FD。

运行期 FIFO 访问由自维护 `fifo.c` 持有，初始化继续使用未修改的供应商驱动。冻结配置固定 FIFO1/2 的深度、起始地址和步长；状态与UA合读，TX按有效负载长度写入，Classic RX读16B，FD先读头及前8B再按DLC补读。Classic收发各三笔SPI，长FD RX四笔；每帧只同步提交一次序列。FIFO 配置在启动时冻结，逐帧访问使用缓存的布局。

一个P1 CAN4线程独占设备，正常流量不需要RX/TX设备互斥锁。只有RAM操作成功才提交UINC；TX按器件要求同一写操作设置UINC和TXREQ。提交结果不确定时保留对象及旧/下一UA，阻止该方向接受另一帧。后续每轮最多一次有界重同步：读回下一UA表示已推进；旧UA才允许重试UINC；其他值隔离该方向，不能自动清空FIFO掩盖丢帧。RX确认后仅投递一次。TX开始UINC提交后所有权交给设备层的在途对象，core不再重发；软件TTL只约束提交前等待，无法确认完成不代表线缆已交付。诊断读取也由同一线程执行。

`Tests/IO/CAN/External` 使用实际初始化、FIFO实现与有限深度寄存器/RAM模型，覆盖所有DLC、FIFO回绕和满空转换、各阶段失败及UINC执行前/后失败。不将这些模型结果当作真实ACK或吞吐证明。

## SPI 仲裁

自维护 SPIAdapter 将 MCP 标记为共享槽内的 CAN 类，整体低于 P0 IMU，同层优先于普通请求；使用四个共享槽，不绕过分发器。两级防饥饿规则见 [SPI](../../IO/SPI/README.md)；真实总线吞吐和错误恢复需实机验收。

器件推进/请求语义依据 [Microchip MCP251XXFD参考手册第4.4节](https://ww1.microchip.com/downloads/en/DeviceDoc/MCP251XXFD-CAN-FD-Controller-Module-Family-Reference-Manual-20005678B.pdf)：追加TX FIFO消息时同写UINC/TXREQ；UA确认基于单一软件拥有者且FIFO配置不变。

## 应用配置示例

应用通过 CAN4 的公开控制器配置使用本器件，不包含供应商头、不自行 Attach SPI。以下片段选择 FD、1 Mbit/s 仲裁和 5 Mbit/s 数据段；实际对端与收发器必须支持同一配置。

```c
#include "SirinEmbedOS/Kernel/System.h"
void configure_can4(SRNOS_SystemConfig *config) { // 从 ApplicationConfigure 调用
    config->can[3].mode = SRNOS_CAN_MODE_FD;
    config->can[3].nominal_bitrate = 1000000;
    config->can[3].data_bitrate = 5000000;
    config->can[3].brs = true;
}
```

报文订阅、发送与统计见 [CAN](../../IO/CAN/README.md) 和 [CANFD 示例](../../../Examples/CANFD/README.md)。维护测试在 SDK 根执行 `bash Tests/IO/CAN/External/run_host_tests.sh`；硬件测试方法见 [验证指南](../../../Docs/Validation.md)，历史实测见 [CHANGELOG](CHANGELOG.md)。
