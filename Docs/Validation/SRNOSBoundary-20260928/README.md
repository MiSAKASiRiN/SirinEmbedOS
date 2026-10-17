# SRNOS 命名、公共边界与运行优化验证：证据阅读指南

核对公开头闭包、符号命名、装配清单和静态内存；同一套构建通过不能代替调度、设备及总线的板端验证。

## 阅读顺序

1. 先读[历史报告与结果表](CHANGELOG.md)，核对日期、固件版本、接线、窗口和未覆盖项。
2. 按报告引用的镜像 SHA、manifest/summary、日志与原始数据复核；中间版本单独比较。
3. 新验证使用独立输出目录，并在对应 `CHANGELOG.md` 追加结果；本说明只维护方法。

本目录常用入口：

- [resources.json](resources.json)
- [debug-sections.txt](debug-sections.txt)
- [host-tests.txt](host-tests.txt)
- [release-sections.txt](release-sections.txt)
- [runtime-names.txt](runtime-names.txt)
- [sdk-check.txt](sdk-check.txt)

## 离线查看示例

从 SDK 根目录执行；下面命令只读取归档，不连接板卡或改写证据。

```sh
python3 -m json.tool Docs/Validation/SRNOSBoundary-20260928/resources.json
rg --files Docs/Validation/SRNOSBoundary-20260928
```

当前实现或测试用法见[接入说明](../../../Tests/Build/README.md)。新测量须重新核对当前源码、镜像与硬件；归档中的暂停、烧录、写 EEPROM 或运动命令不是本页的自动执行步骤。
