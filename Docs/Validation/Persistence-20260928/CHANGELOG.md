# 验证记录

本文保存对应归档的原始报告。日期、版本、通过范围和结束状态仅对当时现场有效，不表示当前板上状态。当前阅读方法见本目录说明文件。

---

<!-- 原报告：README.md；原文 SHA256：d83d2d014f4b110a0ad4c960580ae4b44d5fea0db71d6e9fa4863cf9e26dab5a -->

# 单份持久化、业务参数与高频变量验证

2026-09-28，ST Arm Clang 21.1.1 / STM32CubeCLT 1.22.0，根 Debug/Release 使用 Minimal。本轮没有烧录、连接调试器、Generate 或改动第三方内核；上次 ADC/CAN/EEPROM 实测不能作为本轮固件的验收结果。

## 当前结果

| 检查 | 结果 |
|---|---|
| 完整能力回归 | [25 套主机测试通过](host-tests.txt)，包含实际 Storage/FT24C128A 状态机、参数、SOURCE V1/V2、INS 及既有 IO/Kernel/USB。 |
| 存储模型 | 空白/旧格式、归属/长度/版本、头/数据/提交页损坏、289 个写入截断点、分区隔离、最大 8064 B/流式末尾 CRC、跨页、无变化不写、管理区保护、全局 BUSY、超时/取消/回绕通过。主机 EEPROM 模型不模拟实际掉电电气行为。 |
| 参数与协议 | 默认/非法浮点/范围、批量原子性、稳定 ID 重排/新增/删除/类型变化、旧记录大于现容量、SAVE 中 WRITE、LOAD 冲突和失败保留 RAM、CONFIG 01–08、冻结权限及错误上下文通过。 |
| 遥测 | 静态构造登记、冻结/重复、C/C++ 四类型、未发布有效性、V1 原编码、V2 1000 Hz/总预算、订阅原子拒绝与替换、同频分包、混频 LATEST、回绕/延迟/断连通过。无上位机产品实现。 |
| IMU | [校准专项与 INS 回归](ins-tests.txt)：有效范围/版本/序列化、单颗损坏、保存不改 active、下次启动应用、期限/回绕/发布前抢占跨截止点、SENSOR 校正后再安装旋转、原始数据保留、未校准降级。加载等待不跳过 heater 路径；实际温控共存待板测。 |
| 公共边界 | [90 个公共头、156 个 C11/C++17 独立/聚合单元及依赖闭包](sdk-check.txt)；13 条真实父应用编译命令不暴露 HAL/RTOS/CMSIS/私有包含目录。 |
| 父项目与资源 | 同一检查通过十种父项目固件、非零 C++ TLS、配置覆盖、MSP/libc 容量覆盖与越界拒绝、IRQ/heap/D3 和第三方哈希。 |
| 命名和结构 | [269 个自维护源码/头及 112 个当前 SDK 对象](runtime-names.txt)；[473 项结构合同、30 唯一 HAL 回调、49 来源模块](structure.txt)。没有增加旧名兼容层。 |
| 固件 | [Debug](debug-build.txt)、[Release](release-build.txt) 及 [Persistence 板测入口](board-fixture-build.txt) 链接成功；[Debug ELF](debug-elf.txt)/[Release ELF](release-elf.txt) 检查通过。 |

专项与整套主机脚本分别使用 ASan/UBSan、并发替身或真实实现模型，不将所有测试描述为硬件仿真。`git diff --check` 仅报告工作区既有 Board/Core 的 CRLF 行；本轮没有改动生成文件。自维护新增修改无空白错误。生成/供应商 CMSIS 中既有 `optimize("Os")` 属性仍有 Clang 忽略警告，未修改其源文件。

## 接口与格式变化

- 父项目用 `Persistent<T>` 声明可写/可保存业务参数，用独立 `Telemetry::Value<T>` 发布只读观测值；旧 Param/access 和共享 volatile 源入口删除。
- 新 EEPROM 分区及记录格式没有旧 24C02 自动迁移。每类只有一份，提交失效后中断可能丢失此前内容；最终验证失败也可能发生于提交成功之后，返回失败不代表回滚。启动不自动写默认值。
- CONFIG 01–05 保留原编码，06 查询代次/dirty/回退，07 只读 PCB 身份，08 查询稳定 ID。SOURCE V2 使用 TELEMETRY **0x10–0x14**，因为原计划的 0x08/0x09 已被既有 IMU/INS 状态占用；V1 与专用流原消息和记录格式保留。
- 默认启动加载和业务 SAVE/LOAD 启用；工厂身份写默认关闭。校准保存仅下次启动生效；当前无校准时融合可有效，但明确 UNCALIBRATED/DEGRADED。

## 固定内存与执行预算

| 链接区 | Debug | Release | 容量 |
|---|---:|---:|---:|
| D1，含 128 KiB RTOS heap 与 8 KiB libc arena | 313568 B | 313632 B | 320 KiB |
| D1 链接余量 | 14112 B | 14048 B | — |
| D2 | 9696 B | 9696 B | 32 KiB |
| D3 | 544 B | 544 B | 16 KiB |
| ITCM | 224 B | 192 B | 64 KiB |
| Flash | 276456 B | 197336 B | 1 MiB |

相对 SRNOSBoundary-20260928 记录，D1 两构建均增加 6624 B；该差值包含此后工作区全部改动，不归因于单个对象。当前 Telemetry 目录固定对象约 3843 B，HostComm 原 source/group 固定对象净减少 417 B，合计净增 3426 B（链接间隙另计）。Storage 读写工作区 3×64 B；Parameters 默认 SAVE 镜像为 392 B，LOAD 只有 32 个暂存值和 12 B 流式片段，不保留 8 KiB 镜像。没有生产期新线程或动态镜像分配。

DTCM heap 65536 B、D1 heap 131072 B、MSP 8192 B 和父项目对象池预算保留。DTCM 链接显示 100% 是固定 MSP 末端预留，不能解释为运行期 heap 用尽。Minimal TLS 段为 0；父项目非零 TLS 另测。D3 起址 0x38000000，ADC 仍位于 0x38000200，32 B 对齐；无新 DMA 区或链接段。完整 sections/ELF SHA 见 [资源记录](resources.json)。

`SRNOS_SystemHeapBudget()` 仍描述启动 heap，新增静态对象在 D1/map 核对，不重复充作 heap。线程栈、优先级和队列没有为腾出空间而缩减。EEPROMBringUp 整片测试的 16 KiB 静态备份使父项目 fixture 超出 D1，因此改为已有私有测试分配器一次申请 32 KiB，含备份和工作区；仅该测试应用有此需求，须在新板测确认实际分配余量，GDB dump 按新指针合同使用。

## 板端待验收

[Tests/Board/Persistence](../../../Tests/Board/Persistence/README.md) 已有独立构建入口，默认不写 EEPROM、不发送 CAN。显式 Arm/Command 后测试业务保存/加载及两颗校准；八个 1 kHz 模拟源持续更新，订阅才发送，CAN1/CAN3 组合负载可单独开启。工厂身份写仍关闭。

后续须先保留整片原数据，再实测 I2C/INS/温控共存、保存总耗时、8 路 1 kHz 与 CDC 停读/断连、CAN 组合负载，以及不同保存阶段真实断电后的完整接受或明确回退。单源 1 kHz 和 8000 标量/s 是调度/准入上限，不保证任意负载下全部交付；LATEST 可替换旧数据。无损历史采集不是该接口合同。

既有循环 hook ≤1 µs、统计增量 ≤1 个百分点、Monitor ≤0.1%、监测临界区 ≤10 µs 均需在本轮固件复测，不能引用旧固件数值作为本轮通过。校准算法、校准精度和上位机产品不在本次实现范围。
