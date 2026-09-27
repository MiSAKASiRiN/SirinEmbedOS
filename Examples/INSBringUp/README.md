# INSBringUp

通过默认公共配置启用正式 INS、四颗传感器和60°C温控。启动音、第一灯黄色1Hz心跳；每秒CDC0摘要及`srnos_ins_bringup`调试副本。关闭周期全任务栈扫描、参数加载和HostLink存储命令。

```sh
cmake -S . -B build/INSBringUp -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSIRINEMBEDOS_EXAMPLE=INSBringUp
cmake --build build/INSBringUp
build/hostlink-venv/bin/python Tools/HostLink/imu_telemetry.py --port CDC1_PATH --streams ins mag pressure
```

温稳、观测、收敛期间Application及通信已经运行，但姿态流尚未发送。保持板子低动态，使用状态查询查看进度，不以USB在线判断INS就绪。GDB暂停会扰动采样和温控；速率窗口内不要暂停。完整合同见[INS](../../Docs/INS.md)，结果见[验证记录](../../Docs/Validation.md)。
