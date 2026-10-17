# 验证记录

本文保存对应归档的原始报告。日期、版本、通过范围和结束状态仅对当时现场有效，不表示当前板上状态。当前阅读方法见本目录说明文件。

---

<!-- 原报告：README.md；原文 SHA256：c3abf1a889e7de6f70e37585dcafcd5708f42924d93d15c17b56cb69c7d7bf97 -->

# 2026-09-29 静态校准阈值调整与实板复测

用户要求提高阈值、忽略遥测问题，且不恢复 EEPROM/固件。仅调整静态校准的工程拒绝阈值；不改变采样ODR、驱动滤波、观察窗口、线程、内存和通信格式。新校准记录及新Minimal Release留在板上。

## 阈值与回归

| 原始样本指标 | 原值 | 新值 |
|---|---:|---:|
| 单轴角速度绝对值 | 0.05 rad/s | 0.10 rad/s |
| 单轴角速度峰峰值 | 0.015 rad/s | 0.12 rad/s |
| 单轴角速度标准差 | 0.003 rad/s | 0.02 rad/s |
| 单轴加速度峰峰值 | 0.30 m/s² | 0.40 m/s² |
| 温度窗口峰峰值 | 0.5°C | 1.0°C |

依据前一轮用户确认静止的实测：陀螺标准差最高约0.0112 rad/s、峰峰值约0.0853 rad/s。新值留出原始采样噪声余量；加速度标准差0.08 m/s²、模长8.8–10.8 m/s²、5秒/至少400样本、年龄/间隔、健康/加热故障及保存校验保持不变。阈值提高会降低对轻微运动的拒绝敏感度，仍须保持物理静止，尤其不能靠IMU判定排除缓慢匀速转动；这不是绝对标定精度证明。

主机新增接近实测噪声并含稀疏峰值的接受用例，保留超瞬时幅度、峰峰值、窗口方差、温漂及所有时效/健康/异常拒绝用例。INS与HostLink协议回归通过，Debug/Release均构建及基础ELF检查通过，结构检查通过。静态布局未变：Release D1=314240 B，D2=9696 B，D3=544 B，Flash=200528 B；Debug D1=314208 B、Flash=280808 B。不重新运行无关的完整主机矩阵。

## 板端过程与结果

先确认CLion无调试会话、无其他J-Link/GDB/串口占用。J-Link V8.96，探针10114514，STM32H723VG，VTref3.30V，SWD4MHz。独立构建目录 `build/static-threshold-20260929/{Debug,Release}`，烧录Release后 `.text` 与向量表比对一致；采样时无调试暂停。

等待温度趋稳后连续执行两次 CONFIG 09（01 03），均完成约5秒静态采样、双记录写入/CRC读回验证，并返回最终OK。

- job 1：5254 ms，两颗各501样本，saved_mask=3，storage_status=0/0，revision=1/1。
- job 2：5281 ms，两颗各501样本，saved_mask=3，storage_status=0/0，revision=2/2。

每次在2.5秒仍活动时重发同一序号，收到ACCEPTED后最终结果与状态查询同一job，无额外job或revision增加。最后一次成功响应后INS仍保持旧活动校准合同；新数据经软件复位才启用。原始响应见 [calibration-results](calibration-results.json)，帧日志与构建校验值见 [artifacts](artifacts.json)。

软件复位后两颗load_status=OK、active_mask=3、pending_restart_mask=0、active_revision=saved_revision=2。依匹配Release map定位活动记录，并用同配置Debug类型布局离线核对，读取活动RAM逐float比较与最后成功响应完全一致；加速度仍为零偏0/比例1。见 [active-validation](active-validation.json) 与 [gdb](gdb.log)。这是启动时经Storage校验加载的板端证据，本轮未另刷维护镜像做独立原始EEPROM dump。

INS重新输出有效姿态，flags中的UNCALIBRATED(0x100)已清除，双IMU健康mask=3。重启观察窗口末次flags=561（VALID+RELATIVE_HEADING+THERMAL_PENDING+WARMUP_TIMEOUT），phase=DEGRADED的原因仍有温稳未保持，不能把校准成功写成所有INS降级原因已消失；missed_deadlines=0、max_execution=31µs、max_period=1025µs，详见 [重启状态](reboot-results.json)。

RTOS heap当前空闲66024 B、历史最低61800 B。Release含有非类型符号，原先尝试的类型化GDB查询不可用，后续按匹配map及离线类型布局核对活动数据；本轮仍未取得HostCmd成功路径的栈水位，不能据没有溢出宣称最坏栈余量。

## 保留状态与边界

没有恢复旧固件或EEPROM。最终运行本次Minimal Release，保留双IMU revision2及其实际零偏；GDB/J-Link退出、目标继续运行。未发送CAN负载或修改遥测实现。实际断电、可控运动反例、长期温度变化和绝对校准精度仍未实测；本次验证了静止完成、保存、活动请求去重及软件复位加载链路。前一轮失败阈值证据保留于 [初次板测](../PersistenceBoard-20260929/README.md)。

释放探针后最终USB复核：phase=RUNNING(5)、flags=545（VALID+RELATIVE_HEADING+已发生的WARMUP_TIMEOUT），当前THERMAL_PENDING及UNCALIBRATED均清除、双IMU健康mask=3。见 [最终运行状态](final-live.json)。本轮10篇相关文档118个本地链接目标校验通过。
