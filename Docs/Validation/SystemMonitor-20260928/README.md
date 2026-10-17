# SystemMonitor 验证：证据阅读指南

分开阅读主机模型、目标测试和构建日志；任务 CPU、ADC 有效性、数据年龄和实际栈水位不可相互替代。

## 阅读顺序

1. 先读[历史报告与结果表](CHANGELOG.md)，核对日期、固件版本、接线、窗口和未覆盖项。
2. 按报告引用的镜像 SHA、manifest/summary、日志与原始数据复核；中间版本单独比较。
3. 新验证使用独立输出目录，并在对应 `CHANGELOG.md` 追加结果；本说明只维护方法。

本目录常用入口：

- [summary.json](summary.json)
- [host-tests.log](host-tests.log)
- [sdk-check.log](sdk-check.log)
- [target-tests.log](target-tests.log)

## 离线查看示例

从 SDK 根目录执行；下面命令只读取归档，不连接板卡或改写证据。

```sh
python3 -m json.tool Docs/Validation/SystemMonitor-20260928/summary.json
rg --files Docs/Validation/SystemMonitor-20260928
```

当前实现或测试用法见[接入说明](../../../Source/Services/SystemMonitor/README.md)。新测量须重新核对当前源码、镜像与硬件；归档中的暂停、烧录、写 EEPROM 或运动命令不是本页的自动执行步骤。
