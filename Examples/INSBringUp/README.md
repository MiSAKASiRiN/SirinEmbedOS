# INSBringUp

通过默认公共配置启用正式 INS、四颗传感器和60°C温控。启动音、第一灯黄色1Hz心跳；每秒CDC0摘要及`srnos_ins_bringup`调试副本。关闭周期全任务栈扫描、参数加载和HostLink存储命令。

```sh
cmake -S . -B build/INSBringUp -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSIRINEMBEDOS_EXAMPLE=INSBringUp
cmake --build build/INSBringUp
build/hostlink-venv/bin/python Tools/HostLink/imu_telemetry.py --port CDC1_PATH --streams ins mag pressure
```

温稳、观测、收敛期间Application及通信已经运行，但姿态流尚未发送。保持板子低动态，使用状态查询查看进度，不以USB在线判断INS就绪。GDB暂停会扰动采样和温控；速率窗口内不要暂停。完整合同见[INS](../../Docs/INS.md)，结果见[验证记录](../../Docs/Validation.md)。

## 托管接口迁移

长期应用工作改为托管 step 描述，周期等待由 SDK 负责；观察 srnos_example_create_result 检查创建结果。 旧 ThreadConfig.entry 不再支持；公共创建/停止/删除及同步用法见 Source/Kernel/README.md。本次编译不意味着重新执行板端测试。

关闭业务参数加载与 HostLink 存储命令不关闭 Storage：本例启动仍读取身份及双 IMU 校准，并将有效校准应用于 INS；无有效校准则降级。见 [存储开关与流程](../../Docs/Storage.md#启动和运行期)。
