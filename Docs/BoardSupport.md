# Board/Support 使用与维护

Board/Support 管理当前固定板卡的接线、资源和启动适配。应用使用公开 SDK API；不能通过包含 Support 私有头绕过总线所有权。具体文件索引见 [Support](../Board/Support/README.md)。

## 职责划分

| 目录 | 应放内容 | 调用方式 |
|---|---|---|
| System | 早期内存、heap/libc、Fault 和启动 glue | 由启动/Kernel 路径调用。 |
| IO | SPI/I2C/CAN/Serial/Ethernet 固定 handle、GPIO、DMA 与 IRQ 接线 | Source/IO 通过私有绑定使用。 |
| Devices | 传感器板内轴变换、加热、指示器和 ADC 接线 | Devices/Services 使用；应用不覆盖固定宏。 |
| USB | USB 板级初始化与资源接线 | USB 服务由 Kernel 装配。 |

服务策略、协议解析、队列和周期 worker 仍分别位于 Source/Services、Devices、IO。固定平台允许内部 HAL/寄存器依赖，但不能向公共应用接口泄露。

## 添加或调整绑定

1. 在 IOC 与现有支撑文件核对资源所有者；一个 IRQ/HAL callback 只有一个入口。
2. 自维护绑定放相应 Support 分组，私有头显式使用分组路径。
3. 源文件登记到 `Board/Support/component.cmake`；相关 include 仅供 SDK 私有实现。
4. 若需生成 HAL，按 [Board](../Board/README.md)使用 generate.py，核对 USER CODE、启动向量、时钟和 DMA 变化。
5. 更新模块用法和示例；修改原因与验证结果只记 Board/Support/CHANGELOG.md。

## 查找接线示例

从 SDK 根目录只读查找 Ethernet 接线和构建装配：

```sh
rg -n 'ethernet|SRNOS_SUPPORT' Board/Support/component.cmake
rg -n 'HAL_ETH|LAN8720|RMII' Board/Support/IO/ethernet_board.c Board/OMNICTL2_3.ioc
cmake --preset Debug -DSIRINEMBEDOS_EXAMPLE=Minimal -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build --preset Debug
python3 Board/generate.py --dry-run
```

`--dry-run` 只打印生成计划，不改 IOC/HAL、不接触硬件。完整生成须检查输出与差异，生成或编译通过不等于物理总线通过。

## 内存与兼容边界

DMA/BDMA buffer 的区域、对齐及 cache 规则属于板级合同；ITCM 软件上下文与 DTCM 堆不能作为通用 DMA 分配器。自定义 NOLOAD 段必须由早期启动显式初始化。修改向量、内存布局或 HAL callback 时检查最终 ELF/map。

历史逐文件审查在 [文档 CHANGELOG](CHANGELOG.md)，旧路径映射在 [迁移索引](Migration/README.md)。原始来源与许可记录保持不变，历史路径不可直接作为新代码 include。
