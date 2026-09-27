# 内核与系统装配

本目录提供 FreeRTOS/CMSIS-RTOS2 接入、系统配置校验与冻结、SDK 线程创建及启动屏障。上游内核位于 ThirdParty/FreeRTOS，板级 heap/中断接入位于 Board/Glue；不在本目录维护业务控制循环。公开合同及父项目接入见根 README 和 `Include/SirinEmbedOS/Kernel/`。

`SRNOS_SystemHeapBudget()` 是保守启动预算，包含线程栈、TCB/TLS、队列和同步对象余量，不代表实际剩余堆或应用运行期分配上限。I2C4 复用原 3 KiB / High6 回调线程作为分发线程，每个四槽请求配一个完成信号量；SPI/I2C 同步及启动余量合计 2560 B。实际运行余量需结合目标固件测量。

验证入口为 `Tests/Kernel/run_host_tests.sh`、`Tests/Build/check_sdk.py` 与 `Docs/Validation.md`。编译或预算校验通过不等于所有设备就绪。
