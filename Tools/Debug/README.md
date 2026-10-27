# 板级时序检查

`check_i2c_timing.py` 是 CubeMX 生成流程使用的离线检查工具。它核对 IOC、生成的 `Board/Core/Src/i2c.c` 与 `Board/Support/IO/i2c_timing.h` 的 I2C4 时序，并按固定时钟、滤波和边沿假设计算 Fast-mode 约束。

```sh
python3 Tools/Debug/check_i2c_timing.py
```

命令在 SDK 根目录执行，不连接或修改 MCU。计算通过不代表实测边沿和板端通信通过；修改 IOC 后应先重新生成。时序依据为 ST AN4235 和 STM32H723 数据手册 DS13313。
