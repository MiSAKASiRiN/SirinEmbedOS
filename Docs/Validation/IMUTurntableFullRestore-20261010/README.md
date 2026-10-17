# 恢复三轴直接双环完整实测：证据阅读指南

分别查看 run14/run15 的摘要、分析图与 GDB/采集日志；核对三轴正返段和最终保持状态，不能把构建或 USB 打开过程当作运动证明。

## 阅读顺序

1. 先读[历史报告与结果表](CHANGELOG.md)，核对日期、固件版本、接线、窗口和未覆盖项。
2. 按报告引用的镜像 SHA、manifest/summary、日志与原始数据复核；中间版本单独比较。
3. 新验证使用独立输出目录，并在对应 `CHANGELOG.md` 追加结果；本说明只维护方法。

本目录常用入口：

- [sha256.txt](sha256.txt)
- [build.log](build.log)
- [initial-usb-open-failure.log](initial-usb-open-failure.log)

## 离线查看示例

从 SDK 根目录执行；下面命令只读取归档，不连接板卡或改写证据。

```sh
sed -n '1,100p' Docs/Validation/IMUTurntableFullRestore-20261010/sha256.txt
rg --files Docs/Validation/IMUTurntableFullRestore-20261010
```

当前实现或测试用法见[接入说明](../../../Examples/IMUTurntableBringUp/README.md)。新测量须重新核对当前源码、镜像与硬件；归档中的暂停、烧录、写 EEPROM 或运动命令不是本页的自动执行步骤。
