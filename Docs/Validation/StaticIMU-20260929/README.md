# 手动静态 IMU 校准验证：证据阅读指南

检查静止/运动拒绝、系数范围、保存失败与下次启动生效的主机覆盖；静态资源和编译不证明真实零偏或精度已改善。

## 阅读顺序

1. 先读[历史报告与结果表](CHANGELOG.md)，核对日期、固件版本、接线、窗口和未覆盖项。
2. 按报告引用的镜像 SHA、manifest/summary、日志与原始数据复核；中间版本单独比较。
3. 新验证使用独立输出目录，并在对应 `CHANGELOG.md` 追加结果；本说明只维护方法。

本目录常用入口：

- [resources.json](resources.json)
- [build-debug.txt](build-debug.txt)
- [build-release.txt](build-release.txt)
- [foundation-debug.txt](foundation-debug.txt)
- [foundation-release.txt](foundation-release.txt)
- [host-tests.txt](host-tests.txt)

## 离线查看示例

从 SDK 根目录执行；下面命令只读取归档，不连接板卡或改写证据。

```sh
python3 -m json.tool Docs/Validation/StaticIMU-20260929/resources.json
rg --files Docs/Validation/StaticIMU-20260929
```

当前实现或测试用法见[接入说明](../../INS.md)。新测量须重新核对当前源码、镜像与硬件；归档中的暂停、烧录、写 EEPROM 或运动命令不是本页的自动执行步骤。
