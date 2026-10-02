# SystemMonitor

固定 H723 板低优先级监测，默认启用；取代 Diagnostics 采样线程，Indicators 不变。单个 Low / 4096 B 线程每秒采集 ADC 五参数并分批读取线程统计。启动配置 `system_monitor_enabled` 冻结后不支持远程修改；禁用时不启动 ADC、不创建本线程，Fault 元数据及 DWT/调度 trace 仍保持。

## 使用与数据合同

公共头为 `SirinEmbedOS/Services/SystemMonitor.h`。任务上下文调用 `SRNOS_SystemMonitorGetSnapshot()`、`SRNOS_SystemMonitorGetTask(slot, out)`；返回值表示参数/调用上下文合法，健康检查 `flags`、`valid_fields` 和 ADC `state`。返回独立副本，不借出内部指针。任务槽为 1..64；槽号与 generation 共同识别一次任务生命周期，CPU 第一次采样尚无基线。超过 3 秒的缓存清 CPU/ADC 有效位并标过期，旧数值仍保留。

```c
SRNOS_SystemMonitorSnapshot s;
if (SRNOS_SystemMonitorGetSnapshot(&s) &&
    (s.adc.valid_fields & SRNOS_MONITOR_TEMPERATURE_VALID)) {
    float mcu_c = s.adc.temperature_c;
    (void)mcu_c;
}
```

ADC 原始 rank 顺序为 PC0 电流、PC2_C 输入电压、MCU 温度、VREFINT、VBAT。物理量分别为 A、V、°C；时刻/时长为 µs，CPU 为 ppm（1,000,000=100%，1,000=0.1%）。VBAT 是 MCU 备份电源脚，非机器人动力电池电压。

换算采用用户确认的 TPS26600 IMON 对 RTN 11.8 kΩ、ADC 前串联 100 kΩ，以及电压上臂 100 kΩ/下臂 10 kΩ。电流 `I=Vadc/(78.4e-6*11800)=Vadc/0.92512`，串联保护电阻不计倍率；低于 50 mA 标 LOW_CURRENT，不对整个量程扣除 2 µA 典型偏置。输入电压倍率 11，VBAT 倍率 4。用 H723 12 位 VREFINT_CAL/TS_CAL1/TS_CAL2 和 3.3 V 出厂条件估算 VDDA，温度在 30/130°C 校准点间插值；不用通用 16 位温度宏。VREFINT 电压由同组校准估算，不能作为独立精度验收基准。满量程通道无效并标饱和；工厂校准非法/VDDA 超出 1.62..3.6 V 时撤销相应物理量有效位。保留原始数值供核对，硬件标称系数不等于仪表标定。

来源：[TI TPS2660 Rev.G §9.3.5.4](https://www.ti.com/lit/ds/symlink/tps2660.pdf)、[ST DS13313](https://www.st.com/resource/en/datasheet/stm32h723vg.pdf)、当前 Board ADC3 配置。电流高源阻抗的实际采样误差、VBAT 分压误差和模拟精度仍需仪表验证。

## 调度与故障

采样使用 BDMA 完成通知等待，转换超时 10 ms；判定使用回调时刻，不用低优先级线程恢复运行的时刻。ISR 仅写状态/时间并通知；浮点换算与快照发布在任务完成。故障保留上一次成功采集时间、序号和数值，清有效位。确认 ADC 和 DMA 均停止后才允许重试/复用缓冲区；停止失败进入 QUARANTINED，停止后续 ADC 操作，但继续任务统计。每次尝试间隔至少一秒，不 Panic、不影响应用启动。首次校准及 HAL 停止过程可能有 HAL 内部有界硬件等待，始终可被高优先级任务/中断抢占。

线程按下一截止时间休眠，没有毫秒常驻轮询；逾期不补发积压采样。每批最多四个任务槽，批次间 osDelay(1)；每槽只在短临界区内验证存活、读取 RTOS 状态/计数，句柄不逃出保护区。每轮跨越多次调度，因此任务数据不宣称同一瞬间的全局原子快照，各记录有自己的采样时间和实际窗口。

统计包含 CPU 比例、状态/优先级、切入/Ready 次数、最大运行片段/Ready 延迟。FreeRTOS 将 ISR 时间归入被中断任务，`runtime_us` 并非纯任务指令耗时；片段最大值是 switch-in 到 switch-out，不是一次控制循环耗时。Ready 延迟仅统计真正进入 Ready 的事件，不是周期抖动或全部抢占等待。DWT 32 位片段计数支持一次回绕，超过一个 CYCCNT 周期的未切换片段不保证准确；64 位累计运行时间独立维护。栈水位为 UINT16_MAX（未测量），默认不扫描栈；HostComm 也不再周期扫栈，栈溢出检测/Fault 保留。

监测输出自身 CPU、采样/快照最长临界区、最大唤醒延迟、跳过周期和槽位溢出计数。10 µs 临界区、0.1% 平均 CPU 是板端验收目标，不是静态代码已证明的保证。首次校准及故障恢复单列，不计稳定阶段平均值。

## 发布与验证

HostComm 独立编码最新快照，不由监测线程等 USB。流 0x0002 为 128 B ADC/系统记录；0x0001 为每秒最多四条 112 B 线程记录（每条包含系统汇总），64 槽最多 16 次报告轮转。参见 [协议与命令行](../../../Docs/HostLink.md)。采样不依赖订阅，断线丢弃待发送旧报告，不回放积压。

`Tests/Services/SystemMonitor/run_host_tests.sh` 覆盖数值、生命周期、时序和协议；固件/SDK 构建记录见 Docs/Validation.md。已按用户授权完成板端低干扰基准、ADC 正常采集/软件故障注入及 CDC 背压/DTR 重连；详见 [板端记录](../../../Docs/Validation/ManagedTasksBoard-20260928/README.md)。仪表精度、物理 ADC 故障和外部 CAN 负载仍待验证。

全面命名/事件等待后的固件复测见 [SRNOS 板测](../../../Docs/Validation/SRNOSBoard-20260928/README.md)：统计开启时 Monitor 平均约 0.0492%、最长监测临界区 2 µs，四种 ADC 软件注入和 CDC 背压/DTR 重连通过。该临界区指标只覆盖监测/循环统计的被测区域，不代表全系统最长关中断时间。

## 托管循环快照

线程名为 `SRNOS_Monitor`，仍 Low/4 KiB。任务副本新增 `cycle`，包含独立 sampled/window、生命周期编号、完成数、执行 min/mean/max、启动延迟、周期偏差、miss/skip 和 hook 诊断成本。读取时检查 ACTIVE/MEASURED/PERIODIC/JITTER_VALID/STALE/SATURATED；无周期的事件/相对等待线程不适用抖动。普通框架等待排除，step 内阻塞/抢占/ISR 计入墙钟。采样跨任务分批，不把轮转间隔当实际统计窗口。

HostComm 增加 0x0003，每秒最多四条 112 B 最新循环明细，64 槽最多 16 秒轮转（连续订阅且链路正常时）；0x0001/0x0002 线格式不变。循环统计开关独立于监测服务；Monitor 关闭时不发布/重置循环窗口，长期累计不是每秒报告，观测开销对照应同时明确两项配置。hook 成本估计和 CPU/临界区验收边界见 Kernel 与 Diagnostics/Runtime。

BDMA normal 模式完成后 HAL 将状态置 READY，但可能仍保留 CCR.EN；板级停止流程在 HAL 停止 ADC 后显式关闭专用 BDMA 通道，再读回 ADC/BDMA 均已停用，才允许复用缓冲。未降低停止确认条件。

2026-09-28 仪表约 24 V/0.091 A 单点对照得到 ADC 均值 24.641 V/0.08986 A，错误计数 0；电压偏高约 2.67%，未据此调整标称倍率。仪表未逐点同步，仍需检查分压与参考电源并做多点验证。见 [原始摘要与边界](../../../Docs/Validation/CAN13-ADC-20260928/README.md)。
