# IMU 转台编码器 BringUp：证据阅读指南

按 manifest 匹配镜像和采集窗口，查看反馈在线、实际 CAN 发送、双 IMU 完整性；手动转动反馈不等于自动控制或校准验收。

## 阅读顺序

1. 先读[历史报告与结果表](CHANGELOG.md)，核对日期、固件版本、接线、窗口和未覆盖项。
2. 按报告引用的镜像 SHA、manifest/summary、日志与原始数据复核；中间版本单独比较。
3. 新验证使用独立输出目录，并在对应 `CHANGELOG.md` 追加结果；本说明只维护方法。

本目录常用入口：

- [manifest.json](manifest.json)
- [capture-after-detach.json](capture-after-detach.json)
- [capture-complete.json](capture-complete.json)
- [capture-uninterrupted.json](capture-uninterrupted.json)
- [gdb.log](gdb.log)

## 离线查看示例

从 SDK 根目录执行；下面命令只读取归档，不连接板卡或改写证据。

```sh
python3 -m json.tool Docs/Validation/IMUTurntable-20261009/manifest.json
rg --files Docs/Validation/IMUTurntable-20261009
```

当前实现或测试用法见[接入说明](../../../Examples/IMUTurntableBringUp/README.md)。新测量须重新核对当前源码、镜像与硬件；归档中的暂停、烧录、写 EEPROM 或运动命令不是本页的自动执行步骤。
