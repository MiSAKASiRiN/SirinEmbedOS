# 调试辅助工具

- `sensor_snapshot.gdb`：传感器调试取证命令；运行前确认目标固件符号和会话所有权。
- `read_stack_dump.py`：解析 FreeRTOS 栈转储，填充值余量只代表采集过的运行路径。
- `check_i2c_timing.py`：读取 IOC、生成的 `Board/Core/Src/i2c.c` 和 Board/Support 的固定 I2C4 TIMINGR，检查三者一致及 IOC 的 Fast-mode 选择；以精确有理数检查 60 MHz、模拟滤波 50..80 ns、DNF=0、边沿不超过 300 ns 时的 setup/hold、SCL high/low 和最高时钟约束。输出 JSON；不连接或修改 MCU，也不代表测得实际边沿。修改 IOC 后若尚未生成，此检查会拒绝旧的生成时序。

时序依据为 ST AN4235 的 TIMINGR 方程及 STM32H723 数据手册 DS13313 Rev 5 表 106。实际板测结果见 [CHANGELOG](CHANGELOG.md) 及 [I2C4 证据阅读指南](../../Docs/Validation/I2C400-20260928/README.md)。

## 离线检查示例

从 SDK 根目录执行：

```sh
python3 Tools/Debug/check_i2c_timing.py
python3 Tools/Debug/read_stack_dump.py GDB_LOG STACK_DUMP_PREFIX
```

第二条命令要求已有 `sensor_snapshot.gdb` 生成的日志与同前缀 RAM 转储；将占位符替换为自己的归档路径。解析不会调用 MCU 函数。填充值仍完整不保证所有运行路径都覆盖；未采集字段不记为零。
