# USART1 Panic 默认输出与转台时延/CPU 实测：证据阅读指南

对照电机/IMU设备时间戳、独立分轴段与滤波敏感性；相对滞后不等于总线单程延迟。CPU 包含遥测观测负载，USART1 Panic 输出需单独核对故障日志。

## 阅读顺序

1. 先读[历史报告与结果表](CHANGELOG.md)，核对日期、固件版本、接线、窗口和未覆盖项。
2. 按报告引用的镜像 SHA、manifest/summary、日志与原始数据复核；中间版本单独比较。
3. 新验证使用独立输出目录，并在对应 `CHANGELOG.md` 追加结果；本说明只维护方法。

本目录常用入口：

- [sha256.json](sha256.json)
- [build.log](build.log)
- [capture-summary.json](capture-summary.json)
- [core-tests.log](core-tests.log)
- [explicit-panic-gdb.log](explicit-panic-gdb.log)
- [explicit-panic-usart1.txt](explicit-panic-usart1.txt)

## 离线查看示例

从 SDK 根目录执行；下面命令只读取归档，不连接板卡或改写证据。

```sh
python3 -m json.tool Docs/Validation/IMUTurntableLatency-20261010/sha256.json
rg --files Docs/Validation/IMUTurntableLatency-20261010
```

当前实现或测试用法见[接入说明](../../../Examples/IMUTurntableBringUp/README.md)。新测量须重新核对当前源码、镜像与硬件；归档中的暂停、烧录、写 EEPROM 或运动命令不是本页的自动执行步骤。
