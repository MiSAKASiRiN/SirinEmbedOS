# 验证记录

本文保存对应归档的原始报告。日期、版本、通过范围和结束状态仅对当时现场有效，不表示当前板上状态。当前阅读方法见本目录说明文件。

---

<!-- 原报告：README.md；原文 SHA256：2633130d797bf9e2b7d143fdb949c15eb243dc97570f001c48d4dfa741b49b61 -->

# SystemMonitor 验证（2026-09-28）

本次为代码、主机和编译链接验证，未连接调试器、未烧录、未执行板端采集。原有 Debug/Release/BringUp ELF 未覆盖，使用 build/SystemMonitor-Debug 与 build/SystemMonitor-Release。产物 SHA256 与内存符号见 summary.json。

- 22 组主机套件通过（host-tests.log）。新增 SystemMonitor 子测试覆盖：物理量换算、H723 12位温度/VDDA补偿、零参考/饱和/低电流/坏校准；ADC完成/错误/超时/晚处理/重试/无法停机隔离；64任务槽溢出、删除及复用 generation、一次DWT回绕、CPU实际窗口、过期副本和任务上下文限制；四槽分批及阻塞；遥测1 Hz节流、拥塞丢弃、断连取消/重新订阅；C/Python二进制对照。target-tests.log 包含最终追加的容量用例。
- 板级ADC适配器以HAL假对象注入验证：32字节对齐缓冲区、关闭HT、TC/OVR错误路径、校准失败、启动失败、HAL停止返回值和硬件EN位双确认、停止后旧回调隔离，以及无效ADC/DMA绑定时拒绝启动/停止操作并隔离。主机保留32字节对齐属性，实际D3段位置独立由固件ELF检查。此测试不证明实际ADC/BDMA时序。
- 根 Minimal Debug/Release 均编译链接通过，ADC缓冲区 adc_samples=0x38000200，10 B payload，D3段总544 B，32字节对齐；沿用 NOLOAD 的显式初始化。链接布局、TLS、异常向量检查通过。
- Tests/Build/check_sdk.py：九个父项目固件、上游哈希、C++/TLS、私有头隔离、MSP/libc预算覆盖与溢出拒绝通过（sdk-check.log）。增加ADC D3位置/对齐与旧HSKP worker缺席断言。
- check_structure.py：393份源/头/工具合同、30个唯一HAL回调、49个来源模块、显式源码覆盖通过。ADC回调只有板级新文件持有，BDMA_Channel2仍走生成IRQ。
- 静态检查：SystemMonitor/Runtime无整表vTaskSuspendAll、周期栈扫描或HAL_ADC_PollForConversion；只在四槽批次间osDelay(1)，非毫秒常驻轮询；缓冲区/快照/基线均静态，运行路径不分配。Indicators源码与本轮开始前备份逐字节一致。

已知限制与待测：

- 稳定阶段平均监测线程CPU≤0.1%、新增临界区≤10 µs仅是验收目标，尚未得到板端数据；不将静态检查等同于低干扰保证。
- 需仪表验证电流/输入电压/VDDA/VBAT，核对温度合理性及板上出厂校准值。100 kΩ串联输入的误差、VBAT内部比例误差及TPS26600低电流区仍需测量。
- 需同一固件配置下对比监测开关前后INS/CAN计数、时序与CPU，以及USB断连、ADC中断丢失/失败恢复和长时间运行。
- 统计是调度片段和Ready延迟，不是控制循环抖动，包含ISR时间归属；64位累计计数与32位单片段计时的边界见模块README。
- git diff --check 的全工作区报告包含本轮前已有的 Board/Core/Src/i2c.c 与 main.c CRLF 行尾空白；未为消除此报告修改生成文件。本轮新增/编辑文件无新增尾随空白。
