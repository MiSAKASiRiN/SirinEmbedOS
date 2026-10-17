# CAN 与应用时钟统一，2026-10-12：证据阅读指南

将主机回归、父项目构建和板端反馈年龄分开阅读；比较 CAN 时间戳与公开毫秒时钟，CAN4/FD 模型结果不外推为真实断线或计时回绕验收。

## 阅读顺序

1. 先读[历史报告与结果表](CHANGELOG.md)，核对日期、固件版本、接线、窗口和未覆盖项。
2. 按报告引用的镜像 SHA、manifest/summary、日志与原始数据复核；中间版本单独比较。
3. 新验证使用独立输出目录，并在对应 `CHANGELOG.md` 追加结果；本说明只维护方法。

本目录常用入口：

- [summary.json](summary.json)
- [after-detach-usb.log](after-detach-usb.log)
- [build-Debug.log](build-Debug.log)
- [build-Release.log](build-Release.log)
- [external.log](external.log)
- [fd.log](fd.log)

## 离线查看示例

从 SDK 根目录执行；下面命令只读取归档，不连接板卡或改写证据。

```sh
python3 -m json.tool Docs/Validation/CANClock-20261012/summary.json
rg --files Docs/Validation/CANClock-20261012
```

当前实现或测试用法见[接入说明](../../../Source/IO/CAN/README.md)。新测量须重新核对当前源码、镜像与硬件；归档中的暂停、烧录、写 EEPROM 或运动命令不是本页的自动执行步骤。
