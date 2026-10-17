# P0/P1 优先级调整：证据阅读指南

比对 IOC 生成差异和线程优先级；静态 P0/P1 配置不证明实际组合负载中无饥饿或期限违约。

## 阅读顺序

1. 先读[历史报告与结果表](CHANGELOG.md)，核对日期、固件版本、接线、窗口和未覆盖项。
2. 按报告引用的镜像 SHA、manifest/summary、日志与原始数据复核；中间版本单独比较。
3. 新验证使用独立输出目录，并在对应 `CHANGELOG.md` 追加结果；本说明只维护方法。

本目录常用入口：

- [artifacts.json](artifacts.json)
- [build-summary.log](build-summary.log)
- [cubemx-changed-files.txt](cubemx-changed-files.txt)
- [cubemx-generate.log](cubemx-generate.log)
- [cubemx-timing-check.log](cubemx-timing-check.log)
- [host.log](host.log)

## 离线查看示例

从 SDK 根目录执行；下面命令只读取归档，不连接板卡或改写证据。

```sh
python3 -m json.tool Docs/Validation/Priority-20261003/artifacts.json
rg --files Docs/Validation/Priority-20261003
```

当前实现或测试用法见[接入说明](../../../Source/Kernel/README.md)。新测量须重新核对当前源码、镜像与硬件；归档中的暂停、烧录、写 EEPROM 或运动命令不是本页的自动执行步骤。
