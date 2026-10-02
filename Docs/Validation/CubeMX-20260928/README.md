# CubeMX CLI 生成验证 · 2026-09-28

用户明确要求尝试 `-q` 脚本及 `project generate`。本轮使用用户已有的 `Board/generate.mx`，未修改该脚本；生成前保存完整 Board（排除 build/.idea）及 Git 工作区内容哈希、diff、status 快照，位置见 `summary.json`。

## 执行

从 Board 目录执行：

```sh
/Applications/STMicroelectronics/STM32CubeMX.app/Contents/MacOS/STM32CubeMX \
  -q /Users/sirin/CLionProjects/SirinEmbedOS/Board/generate.mx
```

脚本依次为 `config load /Users/sirin/CLionProjects/SirinEmbedOS/Board/OMNICTL2_3.ioc`、`project generate`、`exit`。用本机附带 UM1718 Rev 50 第 3.3.2 节核对了 `-q` 的无界面脚本语法。实际进程日志为 CubeMX 6.18.1 / DB.6.0.181 / Project Generator 4.10.0-B28；macOS bundle 中旧的 6.13 字段不代表本次运行版本。

CLI 正常返回 `OK`、`Bye bye`，进程退出码 0；完整输出保存在 `generate.log`。日志含第三方更新锁等待、FDCAN 参数引用和 DEBUG/MEMORYMAP 配置元数据警告，随后正常生成结束；没有把这些警告视为板端故障。

## 生成差异

在后续文档与检查脚本修改前，对生成前后全仓库未忽略文件及完整 Board 文件分别进行内容比较，**CubeMX 唯一的文件内容修改是 `Board/Core/Src/i2c.c` 中一行**：

```diff
-  hi2c4.Init.Timing = 0x107075B0;
+  hi2c4.Init.Timing = 0x10C7182D;
```

见 `generation.diff`。新值与 IOC 和此前已板测的 Glue 常量一致。407 个 USER CODE 区域逐一比对一致，原时钟/滤波检查入口保留。IOC、HAL/CMSIS、中断文件、生成 CMake/工具链、SDK 自维护 CMake/Glue/链接脚本及所有其他 Board 文件内容均未被生成器改变。

随后仅更新文档、Glue 头文件说明及 `check_i2c_timing.py`：检查脚本现要求 IOC、生成代码和 Glue 三处 TIMING 一致。未重构 Glue 的启动检查/固定值应用行为。

## 验证

- `python3 Tools/Debug/check_i2c_timing.py` 通过，结果见 `timing-check.json`。
- 独立目录 `build/MXGenerateDebug`、`build/MXGenerateRelease` 的 Minimal 固件配置、编译和链接通过，未覆盖此前 GDB 使用的 EEPROMBringUp ELF。构建日志为 `build-debug.log`、`build-release.log`。
- `python3 Tests/Build/check_sdk.py` 通过：九个父项目固件、C++/TLS、配置覆盖、IRQ/heap/链接段、第三方哈希及容量溢出拒绝，见 `sdk-check.log`。
- Debug：D1 300416 B、D2 9696 B、D3 512 B、Flash 250176 B；Release Flash 176328 B。现有 CMSIS `optimize` 属性警告仍在。固件 SHA-256 见 `summary.json`。
- `git -c core.whitespace=cr-at-eol diff --check` 通过，保留生成 C 文件的 CRLF。

本轮只生成、静态检查及构建，**未烧录、复位、暂停或操作 GDB/J-Link**。板上保留此前 Fast-mode EEPROMBringUp 和测试数据；不能把本次构建当成新一轮板端验收。此前 21 项板测与数字 SCL 测量见 [I2C400-20260928](../I2C400-20260928/README.md)。未提交或推送本轮改动。

## 后续系统调用脚本验证

新增 `Board/generate.py`，首次采用相对 IOC 路径，即便 `subprocess` 的 cwd 为 Board，CubeMX 仍报 IOC 不存在并弹出错误窗口；CLI 的 config load 和 project generate 均返回 KO，进程却退出为 0。包装脚本正确判为失败，Board 内容未变。失败记录位于 `build/cubemx/20260928-030007-7swykui6`，因此保留对生成 OK 的检查，不能只看进程退出码。

修正为根据 `__file__` 动态生成带引号的绝对 IOC 路径后，从 `/tmp` 执行 `python3 /Users/sirin/CLionProjects/SirinEmbedOS/Board/generate.py`。包装脚本退出 0，Generate 成功、时序检查通过、Board 内容变化为空；记录与完整备份位于 `build/cubemx/20260928-030122-za1yfhwr`。`--dry-run` 已验证。没有更改原手工 `generate.mx`；本次幂等生成未修改固件源码，无需重复此前构建或板测。
