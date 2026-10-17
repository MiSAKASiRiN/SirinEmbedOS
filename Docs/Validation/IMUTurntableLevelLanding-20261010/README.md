# IMU调平与正常退出受控落座：证据阅读指南

按 run19/run20/run21/run22 分开阅读；区分调平、保持承重、下放冲击与定时结束。阶段结束或 ACK 不证明机械落座，也不能证明可以断电。

## 阅读顺序

1. 先读[历史报告与结果表](CHANGELOG.md)，核对日期、固件版本、接线、窗口和未覆盖项。
2. 按报告引用的镜像 SHA、manifest/summary、日志与原始数据复核；中间版本单独比较。
3. 新验证使用独立输出目录，并在对应 `CHANGELOG.md` 追加结果；本说明只维护方法。

本目录常用入口：

- [run19/summary.json](run19/summary.json)
- [run20/summary.json](run20/summary.json)
- [run21/summary.json](run21/summary.json)
- [run22/summary.json](run22/summary.json)

## 离线查看示例

从 SDK 根目录执行；下面命令只读取归档，不连接板卡或改写证据。

```sh
python3 -m json.tool Docs/Validation/IMUTurntableLevelLanding-20261010/run19/summary.json
rg --files Docs/Validation/IMUTurntableLevelLanding-20261010
```

当前实现或测试用法见[接入说明](../../../Examples/IMUTurntableBringUp/README.md)。新测量须重新核对当前源码、镜像与硬件；归档中的暂停、烧录、写 EEPROM 或运动命令不是本页的自动执行步骤。
