# 转台上位机开发模式 / revision21：证据阅读指南

区分电机旁路、CAN 零命令、INS 状态与姿态流；phase=0 不表示 INS 无效，姿态质量须读状态标志和序号。

## 阅读顺序

1. 先读[历史报告与结果表](CHANGELOG.md)，核对日期、固件版本、接线、窗口和未覆盖项。
2. 按报告引用的镜像 SHA、manifest/summary、日志与原始数据复核；中间版本单独比较。
3. 新验证使用独立输出目录，并在对应 `CHANGELOG.md` 追加结果；本说明只维护方法。

本目录常用入口：

- [sha256.json](sha256.json)
- [capture-metadata.json](capture-metadata.json)
- [final-ins-status.json](final-ins-status.json)
- [flash.log](flash.log)
- [motor-summary.json](motor-summary.json)

## 离线查看示例

从 SDK 根目录执行；下面命令只读取归档，不连接板卡或改写证据。

```sh
python3 -m json.tool Docs/Validation/IMUTurntableINS-20261010/sha256.json
rg --files Docs/Validation/IMUTurntableINS-20261010
```

当前实现或测试用法见[接入说明](../../../Examples/IMUTurntableBringUp/README.md)。新测量须重新核对当前源码、镜像与硬件；归档中的暂停、烧录、写 EEPROM 或运动命令不是本页的自动执行步骤。
