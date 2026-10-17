# I2C4 周期采集与 EEPROM 并发验证：证据阅读指南

区分首次与最终连续性窗口和两组备份/恢复文件；查看 LIS/SPA 的 READY、有效样本增量、最大间隔及总线错误，暂停取证不属于连续测量窗口。

## 阅读顺序

1. 先读[历史报告与结果表](CHANGELOG.md)，核对日期、固件版本、接线、窗口和未覆盖项。
2. 按报告引用的镜像 SHA、manifest/summary、日志与原始数据复核；中间版本单独比较。
3. 新验证使用独立输出目录，并在对应 `CHANGELOG.md` 追加结果；本说明只维护方法。

本目录常用入口：

- [summary.json](summary.json)
- [build-debug.log](build-debug.log)
- [build-release.log](build-release.log)
- [configure-release.log](configure-release.log)
- [gdb.log](gdb.log)
- [host-tests.log](host-tests.log)

## 离线查看示例

从 SDK 根目录执行；下面命令只读取归档，不连接板卡或改写证据。

```sh
python3 -m json.tool Docs/Validation/I2C-20260928/summary.json
rg --files Docs/Validation/I2C-20260928
```

当前实现或测试用法见[接入说明](../../../Source/IO/I2C/README.md)。新测量须重新核对当前源码、镜像与硬件；归档中的暂停、烧录、写 EEPROM 或运动命令不是本页的自动执行步骤。
