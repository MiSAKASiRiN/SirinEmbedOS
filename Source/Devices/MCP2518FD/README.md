# MCP2518FD 设备驱动

寄存器/协议实现保留原源码许可，SPI 事务统一经 SDK SPI 通路。`SRNOS_MCP2518FDAttachSPI` 继续显式声明 bus/CS；未绑定的默认设备通过 Board/Support/IO/can_board 查询，与外置 CAN 初始化共享同一份硬件配置。无配置的设备返回原错误，不尝试其他引脚。驱动不包含 router 私有上下文；CAN 路由与调度仍属于 Source/IO/CAN。

主机测试使用实际板级配置实现配合 HAL/SPI 桩，覆盖默认、无效 ID、显式 Attach 及覆盖默认绑定；不证明实际 CAN 总线运行。

自维护 SPI 适配实现为 `SPIAdapter.c`，容量和超时宏使用 `SRNOS_MCP2518FD_*`；`DRV_SPI_TransferData` 仅作为 Microchip 驱动规定的私有适配入口保留。原驱动 `.c` 与三个寄存器/API 头按文件单独记录供应商例外，不将整个设备目录视为供应商代码。
