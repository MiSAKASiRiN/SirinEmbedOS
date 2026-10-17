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

## 离线检查示例

从 SDK 根目录运行；编译检查需要 QuickStart 中的工具链 PATH。

```sh
cmake --preset Debug -DSIRINEMBEDOS_EXAMPLE=Minimal -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
python3 Tests/Build/check_structure.py --compile-commands build/Debug/compile_commands.json
python3 Tests/Build/check_sdk.py --public-headers-only
bash Tests/Kernel/run_host_tests.sh
bash Tests/Services/Network/run_host_tests.sh
```

根据改动选择相关套件；完整主机套件入口是 `python3 Tests/run_host_tests.py`。完整 SDK 集成检查使用 `python3 Tests/Build/check_sdk.py`，会构建测试父项目并检查链接布局和第三方哈希，不访问 PCB。

构建隔离的观测固件示例：

```sh
cmake -S . -B build/bringup-check -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSIRINEMBEDOS_EXAMPLE=BringUp
cmake --build build/bringup-check -j 8
```

仅生成固件，不自动下载。所选示例、配置和 ELF 哈希应与后续验证记录对应。

## 资源与板端验收

- map 给出静态分配和预留，不能代替 heap 当前/历史最小余量或任务栈水位。常规监控不周期扫栈；专用观测会扰动调度。
- DMA staging 保持可达 SRAM、对齐和 cache 合同；ITCM/DTCM 不能随意用于 DMA。链接段调整需核对早期清零/复制。
- 系统启动、设备 READY、有效数据、吞吐、期限和故障恢复分别取证，不将其中一项视作全部验收。
- 实机复现遵循 [验证指南](Validation.md)和具体测试说明，保存原固件/配置及可恢复状态；不依据历史结束状态操作当前调试会话。
- 未实现的接口不在文档中承诺；FatFs、硬件看门狗监督等能力应先确认真实需求，再设计实现与验收。

已记录问题及阶段性实测保留在 [文档 CHANGELOG](CHANGELOG.md)和 [验证历史](Validation/CHANGELOG.md)。这些条目用于定位证据，不能自动外推为当前固件已修复或仍失败。

<a id="2026-10-03-初检遗留问题"></a>

旧文档链接的初检问题清单位于 [CHANGELOG 历史条目](CHANGELOG.md#2026-10-03-初检遗留问题)。使用该清单时，先按当前源码复现，再判断是否仍需修复。
