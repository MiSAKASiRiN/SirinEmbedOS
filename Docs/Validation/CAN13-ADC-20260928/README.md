# ADC 对照与 CAN1/CAN3 吞吐实测：证据阅读指南

先看 ADC 电压单位、参考源和误差口径，再看请求帧率、实际窗口、对侧接收及 CPU；环回无丢失与业务任务拥有足够 CPU 余量是两项判定。

## 阅读顺序

1. 先读[历史报告与结果表](CHANGELOG.md)，核对日期、固件版本、接线、窗口和未覆盖项。
2. 按报告引用的镜像 SHA、manifest/summary、日志与原始数据复核；中间版本单独比较。
3. 新验证使用独立输出目录，并在对应 `CHANGELOG.md` 追加结果；本说明只维护方法。

本目录常用入口：

- [adc-metadata.json](adc-metadata.json)
- [adc-summary.json](adc-summary.json)
- [app-compile.json](app-compile.json)
- [before-notifications.txt](before-notifications.txt)
- [can-host.log](can-host.log)
- [final-restored-metadata.json](final-restored-metadata.json)

## 离线查看示例

从 SDK 根目录执行；下面命令只读取归档，不连接板卡或改写证据。

```sh
python3 -m json.tool Docs/Validation/CAN13-ADC-20260928/adc-metadata.json
rg --files Docs/Validation/CAN13-ADC-20260928
```

当前实现或测试用法见[接入说明](../../../Tests/Board/CANThroughput/README.md)。新测量须重新核对当前源码、镜像与硬件；归档中的暂停、烧录、写 EEPROM 或运动命令不是本页的自动执行步骤。
