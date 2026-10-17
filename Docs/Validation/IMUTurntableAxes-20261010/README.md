# 多姿态电机轴与板载IMU坐标辨识：证据阅读指南

按静止、独立分轴和留出联动段分组，检查轴顺序、坐标系、原点、积分残差和样本完整性；拟合的机械轴向不等于已经写入 SDK 或完成精密标定。

## 阅读顺序

1. 先读[历史报告与结果表](CHANGELOG.md)，核对日期、固件版本、接线、窗口和未覆盖项。
2. 按报告引用的镜像 SHA、manifest/summary、日志与原始数据复核；中间版本单独比较。
3. 新验证使用独立输出目录，并在对应 `CHANGELOG.md` 追加结果；本说明只维护方法。

本目录常用入口：

- [sha256.txt](sha256.txt)
- [summary.json](summary.json)
- [axis-calibration.json](axis-calibration.json)
- [axis_calibration_plan.json](axis_calibration_plan.json)
- [build.log](build.log)
- [catalog.json](catalog.json)

## 离线查看示例

从 SDK 根目录执行；下面命令只读取归档，不连接板卡或改写证据。

```sh
python3 -m json.tool Docs/Validation/IMUTurntableAxes-20261010/summary.json
rg --files Docs/Validation/IMUTurntableAxes-20261010
```

当前实现或测试用法见[接入说明](../../../Examples/IMUTurntableBringUp/README.md)。新测量须重新核对当前源码、镜像与硬件；归档中的暂停、烧录、写 EEPROM 或运动命令不是本页的自动执行步骤。
