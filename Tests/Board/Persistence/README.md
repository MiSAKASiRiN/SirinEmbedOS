# 单份持久化与高频变量板测入口

2026-09-29 首次实板记录见 [PersistenceBoard](../../../Docs/Validation/PersistenceBoard-20260929/README.md)：参数保存/重启、代次隔离通过；校准静止误拒绝和实际遥测速率未达目标仍待处理。未发送CAN帧，原EEPROM已恢复。

本工程仅用于获授权后的独立板测，不是默认固件。所有应用源码只包含 SDK 公共头；启动不写 EEPROM、不产生 CAN 报文。注册八个 1 kHz 编码器模拟源、两个业务参数，并运行正常 INS、温控及环境传感器。

```sh
cmake -S Tests/Board/Persistence -B build/Persistence -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$PWD/CMake/starm-clang.cmake"
cmake --build build/Persistence -j8
```

实际写入前必须读取并在主机保存整片原内容，确认后由调试器将 `SRNOS_TestPersistenceArm` 设为 `0x53524E56`。`SRNOS_TestPersistenceCommand` 每次只执行一次：1 修改测试 kp 并 SAVE；2 LOAD；3 仅恢复 RAM 默认；4/5 分别保存 BMI/LSM 的单位校正。校准命令只是编码/重启验收数据，不代表已完成物理校准。写入会覆盖该区域唯一记录，结束后在维护模式恢复原 EEPROM 内容并恢复正常固件。

测试矩阵：SAVE/LOAD/重启核对默认与保存值；保存后修改 RAM 检查 dirty；保存单位校正后重启才生效；写入期间在多个阶段实际断电，重启应接受完整记录或明确回退。不要在断电后自动重复写入掩盖现场。观察 `SRNOS_TestPersistenceCompleted/Result/INS/Monitor`，同时核对传感器序号及错误计数。PCB 序列号工厂写权限保持关闭。

HostComm SOURCE V2（TELEMETRY 0x10–0x14）可订阅八源各 1000 Hz；停读、断连、重连并观察回传的采样时间/序号和跳过计数。没有订阅时生产者仍更新缓存，不发送数据。本轮不提供上位机产品，协议编码可复用主机测试或独立测试客户端。

仅在 CAN1/CAN3 接线和负载已确认时，设置 `SRNOS_TestPersistenceCANFramesPerMs=1..3` 加入每方向 1–3 kframe/s；0 停止，取消 Arm 也停止。此入口只提供组合负载，完整序号/吞吐验证使用相邻 CANThroughput 工程。比较保存前后 INS 周期、环境采样、CAN 错误和 CPU；记录新增临界区时长。未执行板测前不得把构建成功记为性能、校准或掉电验收。
