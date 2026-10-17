# Board/Support 整理验证：证据阅读指南

对照职责和构建装配检查，确认生成文件、Support 私有接线及服务层的所有权；该组材料主要是离线检查，不能据此判定外设已经运行。

## 阅读顺序

1. 先读[历史报告与结果表](CHANGELOG.md)，核对日期、固件版本、接线、窗口和未覆盖项。
2. 按报告引用的镜像 SHA、manifest/summary、日志与原始数据复核；中间版本单独比较。
3. 新验证使用独立输出目录，并在对应 `CHANGELOG.md` 追加结果；本说明只维护方法。

## 离线查看示例

从 SDK 根目录执行；下面命令只读取归档，不连接板卡或改写证据。

```sh
sed -n '1,160p' Docs/Validation/BoardSupport-20260928/CHANGELOG.md
rg --files Docs/Validation/BoardSupport-20260928
```

当前实现或测试用法见[接入说明](../../../Board/Support/README.md)。新测量须重新核对当前源码、镜像与硬件；归档中的暂停、烧录、写 EEPROM 或运动命令不是本页的自动执行步骤。
