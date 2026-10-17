# 托管任务与 SystemMonitor 板端验证：证据阅读指南

对照开关构建的相同负载窗口，查看生命周期计数、Idle CPU、hook 开销和 ADC 故障注入；注入停止确认失败不等于制造真实 DMA 卡死。

## 阅读顺序

1. 先读[历史报告与结果表](CHANGELOG.md)，核对日期、固件版本、接线、窗口和未覆盖项。
2. 按报告引用的镜像 SHA、manifest/summary、日志与原始数据复核；中间版本单独比较。
3. 新验证使用独立输出目录，并在对应 `CHANGELOG.md` 追加结果；本说明只维护方法。

本目录常用入口：

- [SHA256SUMS](SHA256SUMS)
- [summary.json](summary.json)
- [elf-debug.log](elf-debug.log)
- [elf-release.log](elf-release.log)
- [gdb.log](gdb.log)
- [host-tests-final.log](host-tests-final.log)

## 离线查看示例

从 SDK 根目录执行；下面命令只读取归档，不连接板卡或改写证据。

```sh
python3 -m json.tool Docs/Validation/ManagedTasksBoard-20260928/summary.json
rg --files Docs/Validation/ManagedTasksBoard-20260928
```

当前实现或测试用法见[接入说明](../../../Tests/Board/ManagedTasks/README.md)。新测量须重新核对当前源码、镜像与硬件；归档中的暂停、烧录、写 EEPROM 或运动命令不是本页的自动执行步骤。
