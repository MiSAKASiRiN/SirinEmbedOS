# 负载窗口阅读指南

按 Classic、FD 默认时序和 aligned 对照分组；核对生产周期不足、MCP 硬件 RX overflow、软件 TTL 和逐帧校验，不能把未达标窗口汇成通过。

## 阅读顺序

1. 先读[历史报告与结果表](CHANGELOG.md)，核对日期、固件版本、接线、窗口和未覆盖项。
2. 按报告引用的镜像 SHA、manifest/summary、日志与原始数据复核；中间版本单独比较。
3. 新验证使用独立输出目录，并在对应 `CHANGELOG.md` 追加结果；本说明只维护方法。

本目录常用入口：

- [manifest.json](manifest.json)
- [summary.json](summary.json)
- [elf-check-final.log](elf-check-final.log)
- [gdb.log](gdb.log)
- [include-check-final.log](include-check-final.log)
- [observations.json](observations.json)

## 离线查看示例

从 SDK 根目录执行；下面命令只读取归档，不连接板卡或改写证据。

```sh
python3 -m json.tool Docs/Validation/CAN4-SPI-20261006/manifest.json
rg --files Docs/Validation/CAN4-SPI-20261006
```

当前实现或测试用法见[接入说明](../../../Tests/Board/CAN4SPILoad/README.md)。新测量须重新核对当前源码、镜像与硬件；归档中的暂停、烧录、写 EEPROM 或运动命令不是本页的自动执行步骤。
