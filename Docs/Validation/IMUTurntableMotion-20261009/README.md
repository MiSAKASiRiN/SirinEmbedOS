# IMU 转台低速控制验证：证据阅读指南

按照 manifest 和 run 编号阅读回位/运动范围、故障与双 IMU 对比；保留中止窗口，不能从部分回位推断三轴校准已完成。

## 阅读顺序

1. 先读[历史报告与结果表](CHANGELOG.md)，核对日期、固件版本、接线、窗口和未覆盖项。
2. 按报告引用的镜像 SHA、manifest/summary、日志与原始数据复核；中间版本单独比较。
3. 新验证使用独立输出目录，并在对应 `CHANGELOG.md` 追加结果；本说明只维护方法。

本目录常用入口：

- [manifest.json](manifest.json)
- [after-detach-summary.json](after-detach-summary.json)
- [build.log](build.log)
- [final-sdk-check.log](final-sdk-check.log)
- [host-tests.log](host-tests.log)
- [run1-summary.json](run1-summary.json)

## 离线查看示例

从 SDK 根目录执行；下面命令只读取归档，不连接板卡或改写证据。

```sh
python3 -m json.tool Docs/Validation/IMUTurntableMotion-20261009/manifest.json
rg --files Docs/Validation/IMUTurntableMotion-20261009
```

当前实现或测试用法见[接入说明](../../../Examples/IMUTurntableBringUp/README.md)。新测量须重新核对当前源码、镜像与硬件；归档中的暂停、烧录、写 EEPROM 或运动命令不是本页的自动执行步骤。
