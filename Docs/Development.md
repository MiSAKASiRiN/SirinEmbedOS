# SDK 开发与验证流程

用于在固定平台上扩展或维护 SDK。父项目入门见 [QuickStart](../QuickStart.md)，接口合同见对应模块 README；变更记录写入模块 CHANGELOG，跨模块或公共合同变更同时记入根 CHANGELOG。

## 选择修改位置

| 需求 | 位置与边界 |
|---|---|
| 机器人控制、状态机、机构参数 | 父项目 App/Config，不加入 SDK 默认服务。 |
| 复用算法、设备协议 | Source/Algorithms 或 Source/Devices；对外声明放 Include/SirinEmbedOS。 |
| 总线仲裁、异步事务与收发队列 | Source/IO；说明期限、buffer 生命周期和调用上下文。 |
| 共享服务或后台 worker | Source/Services；由 Kernel 装配，应用只声明配置。 |
| 引脚、handle、DMA、IRQ、启动/内存适配 | Board/Support；按 [BoardSupport](BoardSupport.md)登记，生成代码遵循 USER CODE 边界。 |
| CubeMX 外设配置 | Board/OMNICTL2_3.ioc；使用 Board/generate.py，检查生成差异。 |

## 实施步骤

1. 查看工作区差异及模块 README/CHANGELOG，确认当前调用链、资源所有者和公共接口；历史问题先复现，不据旧日志假定仍存在。
2. 公共 API 写清单位、返回值、超时、任务/ISR 上下文和所有权。启动配置完成校验后冻结，恢复不能重绑硬件架构。
3. 修改显式 component.cmake 清单，不依赖目录扫描装配。不要为假想硬件增加 backend 或为每个接口新增线程。
4. 用主机测试覆盖实际行为；涉及队列/并发时覆盖容量、超时、错误和回绕。示例标明完整程序还是集成片段。
5. 构建相关固件；资源变化检查 ELF/map，硬件行为变化另做对应实板验证。未连接 PCB 时明确停留在离线证据。
6. 将最终用法和示例更新到 README；本次原因、兼容性、检查结果和待验事项只记 CHANGELOG。

## 父项目任务开发约定

任务拆成 init / step / cleanup，优先通过 ThreadCreate 保存可管理句柄。先准备资源再启用生产者；Create 成功与 init 成功分开检查。停止生产者后 RequestStop / Join / Delete，只有 Delete 成功才释放参数。生命周期完整示例见 [Kernel](../Source/Kernel/README.md)。

## 构建检查

从 SDK 根目录运行；需要 QuickStart 中的工具链 PATH：

```sh
cmake --preset Debug -DSIRINEMBEDOS_EXAMPLE=Minimal
cmake --build --preset Debug -j 8
cmake --preset Release -DSIRINEMBEDOS_EXAMPLE=Minimal
cmake --build --preset Release -j 8
python3 Tools/Debug/check_i2c_timing.py
```

Minimal 和 InputMessages 可通过 `SIRINEMBEDOS_EXAMPLE` 选择。其余 API 用法示例由父项目按需接入。构建只生成固件，不自动下载；父项目应独立配置和构建，核对公共头隔离、最终 ELF 与资源预算。

Git 分发不包含测试套件、专用实机测试固件或原始验证材料。本地已配置私有测试目录时，可从 SDK 根目录运行 `python3 Tests/run_host_tests.py`；这不是公开检出的必需构建步骤。私有测试源码和 BringUp 依赖必须保持忽略且未跟踪。历史变更记录只说明当时的实现与验证范围。

## 资源与板端验收

- map 给出静态分配和预留，不能代替 heap 当前/历史最小余量或任务栈水位。常规监控不周期扫栈；专用观测会扰动调度。
- DMA staging 保持可达 SRAM、对齐和 cache 合同；ITCM/DTCM 不能随意用于 DMA。链接段调整需核对早期清零/复制。
- 系统启动、设备 READY、有效数据、吞吐、期限和故障恢复分别取证，不将其中一项视作全部验收。

- 未实现的接口不在文档中承诺；FatFs、硬件看门狗监督等能力应先确认真实需求，再设计实现与验收。

历史记录不能自动外推为当前固件已修复或仍失败。

<a id="2026-10-03-初检遗留问题"></a>

旧文档链接的初检问题清单位于 [CHANGELOG 历史条目](CHANGELOG.md#2026-10-03-初检遗留问题)。使用该清单时，先按当前源码复现，再判断是否仍需修复。
