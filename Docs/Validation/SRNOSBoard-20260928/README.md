# SRNOS 命名、公共隔离与事件等待板端复测：证据阅读指南

使用 comparison、ADC 故障及 CDC 元数据区分统计对照、生命周期和遥测恢复；温稳、INS VALID 与正常固件独立采集需分别核对。

## 阅读顺序

1. 先读[历史报告与结果表](CHANGELOG.md)，核对日期、固件版本、接线、窗口和未覆盖项。
2. 按报告引用的镜像 SHA、manifest/summary、日志与原始数据复核；中间版本单独比较。
3. 新验证使用独立输出目录，并在对应 `CHANGELOG.md` 追加结果；本说明只维护方法。

本目录常用入口：

- [adc-fault-results.json](adc-fault-results.json)
- [application-compile-command.json](application-compile-command.json)
- [capture-manifest.json](capture-manifest.json)
- [cdc-metadata.json](cdc-metadata.json)
- [comparison.json](comparison.json)
- [final-ins-status.json](final-ins-status.json)

## 离线查看示例

从 SDK 根目录执行；下面命令只读取归档，不连接板卡或改写证据。

```sh
python3 -m json.tool Docs/Validation/SRNOSBoard-20260928/adc-fault-results.json
rg --files Docs/Validation/SRNOSBoard-20260928
```

当前实现或测试用法见[接入说明](../../../Tests/Build/README.md)。新测量须重新核对当前源码、镜像与硬件；归档中的暂停、烧录、写 EEPROM 或运动命令不是本页的自动执行步骤。
