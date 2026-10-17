# 转台校准保存栈溢出修复：证据阅读指南

用 original-gdb 与 fault-confirmed 追踪保存故障，再对照 fixed-gdb、save-result 和 coefficients-verified；保存成功与重启加载一致性、运动复测是不同结论。

## 阅读顺序

1. 先读[历史报告与结果表](CHANGELOG.md)，核对日期、固件版本、接线、窗口和未覆盖项。
2. 按报告引用的镜像 SHA、manifest/summary、日志与原始数据复核；中间版本单独比较。
3. 新验证使用独立输出目录，并在对应 `CHANGELOG.md` 追加结果；本说明只维护方法。

本目录常用入口：

- [sha256.json](sha256.json)
- [summary.json](summary.json)
- [coefficients-verified.json](coefficients-verified.json)
- [final-idle.json](final-idle.json)
- [final-ins.json](final-ins.json)
- [fixed-gdb.log](fixed-gdb.log)

## 离线查看示例

从 SDK 根目录执行；下面命令只读取归档，不连接板卡或改写证据。

```sh
python3 -m json.tool Docs/Validation/IMUTurntableSaveFault-20261010/sha256.json
rg --files Docs/Validation/IMUTurntableSaveFault-20261010
```

当前实现或测试用法见[接入说明](../../../Examples/IMUTurntableBringUp/README.md)。新测量须重新核对当前源码、镜像与硬件；归档中的暂停、烧录、写 EEPROM 或运动命令不是本页的自动执行步骤。
