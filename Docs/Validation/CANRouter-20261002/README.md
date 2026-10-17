# CAN Router 每路 3 kHz 双向目标：离线优化：证据阅读指南

检查虚拟负载的队列、计数、通知竞争与构建布局；模拟时间下的每路负载只证明软件合同，不代表 MCU 吞吐或混合负载时延。

## 阅读顺序

1. 先读[历史报告与结果表](CHANGELOG.md)，核对日期、固件版本、接线、窗口和未覆盖项。
2. 按报告引用的镜像 SHA、manifest/summary、日志与原始数据复核；中间版本单独比较。
3. 新验证使用独立输出目录，并在对应 `CHANGELOG.md` 追加结果；本说明只维护方法。

本目录常用入口：

- [artifacts.json](artifacts.json)
- [build-debug.log](build-debug.log)
- [build-release.log](build-release.log)
- [cubemx-changed-files.txt](cubemx-changed-files.txt)
- [cubemx-generate.log](cubemx-generate.log)
- [cubemx-timing-check.log](cubemx-timing-check.log)

## 离线查看示例

从 SDK 根目录执行；下面命令只读取归档，不连接板卡或改写证据。

```sh
python3 -m json.tool Docs/Validation/CANRouter-20261002/artifacts.json
rg --files Docs/Validation/CANRouter-20261002
```

当前实现或测试用法见[接入说明](../../../Source/IO/CAN/README.md)。新测量须重新核对当前源码、镜像与硬件；归档中的暂停、烧录、写 EEPROM 或运动命令不是本页的自动执行步骤。
