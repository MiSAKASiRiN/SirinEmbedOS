# CAN4 单服务与 SPI 序列优化验证：证据阅读指南

以 classic-final 及对应 manifest 为正式 Classic 验收入口；classic-quick、v2、profile 与 long-a 用于定位或中间对照。FD 逐档检查请求速率、交付、TTL、IMU 保护线；SPI 峰值与窗口增量分开解释。

## 阅读顺序

1. 先读[历史报告与结果表](CHANGELOG.md)，核对日期、固件版本、接线、窗口和未覆盖项。
2. 按报告引用的镜像 SHA、manifest/summary、日志与原始数据复核；中间版本单独比较。
3. 新验证使用独立输出目录，并在对应 `CHANGELOG.md` 追加结果；本说明只维护方法。

本目录常用入口：

- [manifest.json](manifest.json)
- [acceptance.json](acceptance.json)
- [changed-files.json](changed-files.json)
- [classic-final-perf.json](classic-final-perf.json)
- [classic-final-summary.json](classic-final-summary.json)
- [classic-long-a-perf.json](classic-long-a-perf.json)

## 离线查看示例

从 SDK 根目录执行；下面命令只读取归档，不连接板卡或改写证据。

```sh
python3 -m json.tool Docs/Validation/CAN4-Optimization-20261006/manifest.json
rg --files Docs/Validation/CAN4-Optimization-20261006
```

当前实现或测试用法见[接入说明](../../../Tests/Board/CAN4SPILoad/README.md)。新测量须重新核对当前源码、镜像与硬件；归档中的暂停、烧录、写 EEPROM 或运动命令不是本页的自动执行步骤。
