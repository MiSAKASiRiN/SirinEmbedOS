# 持久化业务参数与只读遥测示例

父项目把 `app_main.cpp` 作为应用源文件，链接 `SirinEmbedOS::Platform`。仅包含 SDK 公共头，不需要 FreeRTOS/CMSIS/HAL。

示例声明三个 `SRNOS::Parameters::Persistent<float>` PID 增益：稳定 ID 分别为 0x1001/0x1002/0x1003，均有默认值、最小/最大值；配置 application_id=`0x50494431`、compatibility_version=1。启动加载成功使用已保存值，否则使用声明默认值。注册表冻结后 HostComm CONFIG 可以读取/修改这些业务值，CAN/UART/IMU 配置不参与参数注册。

另一个 `SRNOS::Telemetry::Value<int32_t>` 声明 `motor.encoder`、单位 count、最大 100 Hz。托管任务每 10 ms 发布示例计数及 `SRNOS_TimeUs()`；真实父项目换为电机采样结果即可。它只保留最新快照，不写 EEPROM、不允许 CONFIG WRITE；未订阅时也不会每次 Publish 唤醒 HostComm。

周期 step 取得 PID 三值的一致 Snapshot，再交给业务逻辑；例子不持有控制器指针、不驱动执行器。长期循环和周期等待由 SDK 托管，检查 `srnos_example_create_result` 确认创建成功。

WRITE 立即更新 RAM；只有显式 SAVE 才持久化。SAVE 在 HostCmd 执行，不阻塞 HostComm 或控制任务。用 STATE 观察 generation/saved_generation/dirty：保存版本 10 期间改到 11，保存完成后仍显示未保存修改。LOAD 遇到并发修改返回冲突，不覆盖新值；恢复默认也只改 RAM，仍需显式 SAVE。EEPROM 只有单份记录，掉电中断保存可能使其失效，下一次启动回退默认。

C 项目可使用 `SRNOS_ParametersRegisterPersistent` 和 `SRNOS_TelemetryRegister/Publish`。登记复制元数据，父项目在启动配置期完成注册，不在运行期新增。详细合同见 [Parameters](../../Source/Services/Parameters/README.md)、[Telemetry](../../Source/Services/Telemetry/README.md) 和 [Kernel](../../Source/Kernel/README.md)。主机/构建检查不代替板端掉电或高频吞吐验收。

完整端到端目录、RAM WRITE、显式 SAVE、运行 LOAD 失败保留现值及 EEPROM 格式见 [存储总览](../../Docs/Storage.md)；主机字段见 [CONFIG 协议](../../Source/Services/Parameters/HostLink/README.md)。

## 父项目装配示例

在父项目已经 `add_subdirectory(SirinEmbedOS)` 后添加：

```cmake
set(SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/SirinEmbedOS")
sirinos_add_firmware(ParameterExample
  "${SDK_DIR}/Examples/Parameters/app_main.cpp")
```

同一固件只保留一份 `SRNOS_ApplicationMain()`，配置回调按项目集中维护。历史变更与验证见 [CHANGELOG](CHANGELOG.md)。
