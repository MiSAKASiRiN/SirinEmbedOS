# CAN1 / CAN3 Classic 与 CAN FD 实板通信、负载：证据阅读指南

先用 manifest 匹配模式和镜像，再按帧长、BRS、位速率和双向请求负载比较交付；排空后的总接收数不能替代活动窗口速率，CAN1/3 结果不代表其他控制器。

## 阅读顺序

1. 先读[历史报告与结果表](CHANGELOG.md)，核对日期、固件版本、接线、窗口和未覆盖项。
2. 按报告引用的镜像 SHA、manifest/summary、日志与原始数据复核；中间版本单独比较。
3. 新验证使用独立输出目录，并在对应 `CHANGELOG.md` 追加结果；本说明只维护方法。

本目录常用入口：

- [manifest.json](manifest.json)
- [summary.json](summary.json)
- [config.log](config.log)
- [gdb.log](gdb.log)
- [result-check.log](result-check.log)
- [structure.log](structure.log)

## 离线查看示例

从 SDK 根目录执行；下面命令只读取归档，不连接板卡或改写证据。

```sh
python3 -m json.tool Docs/Validation/CANFD-Board-20261006/manifest.json
rg --files Docs/Validation/CANFD-Board-20261006
```

当前实现或测试用法见[接入说明](../../../Tests/Board/CANFDLoad/README.md)。新测量须重新核对当前源码、镜像与硬件；归档中的暂停、烧录、写 EEPROM 或运动命令不是本页的自动执行步骤。
