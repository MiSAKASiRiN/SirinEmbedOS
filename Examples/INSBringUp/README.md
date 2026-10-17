# INSBringUp

通过默认公共配置启用正式 INS、四颗传感器和60°C温控。启动音、第一灯黄色1Hz心跳；每秒CDC0摘要及`srnos_ins_bringup`调试副本。关闭周期全任务栈扫描、参数加载和HostLink存储命令。

```sh
cmake -S . -B build/INSBringUp -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSIRINEMBEDOS_EXAMPLE=INSBringUp
cmake --build build/INSBringUp
build/hostlink-venv/bin/python Tools/HostLink/imu_telemetry.py --port CDC1_PATH --streams ins mag pressure
```

温稳、观测、收敛期间Application及通信已经运行，但姿态流尚未发送。保持板子低动态，使用状态查询查看进度，不以USB在线判断INS就绪。GDB暂停会扰动采样和温控；速率窗口内不要暂停。完整合同见[INS](../../Docs/INS.md)，结果见[验证记录](../../Docs/Validation.md)。

## 任务接入

应用使用托管 `step` 回调，周期等待由 SDK 负责。检查 `srnos_example_create_result` 确认任务创建结果；创建、停止、删除与同步对象用法见 [Kernel](../../Source/Kernel/README.md)。

关闭业务参数加载与 HostLink 存储命令不关闭 Storage：本例启动仍读取身份及双 IMU 校准，并将有效校准应用于 INS；无有效校准则降级。见 [存储开关与流程](../../Docs/Storage.md#启动和运行期)。

修改与验证记录见 [CHANGELOG](CHANGELOG.md)。
