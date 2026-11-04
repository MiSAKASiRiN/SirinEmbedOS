# VT02VT12

通过公开 API 配置 115200/8N1 UART9，接收 `vt02` 键鼠 Input 与 `0302` 的 30 B 自定义 Message。完整应用在 [app_main.c](app_main.c)，不注册执行器，不进行键鼠/摇杆映射。加热和 INS 融合在该示例关闭，SDK 固定传感器服务仍会采样。

```sh
cmake -S . -B build/VT02VT12 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=CMake/starm-clang.cmake \
  -DCMAKE_BUILD_TYPE=Release -DSIRINEMBEDOS_EXAMPLE=VT02VT12
cmake --build build/VT02VT12 -j8
```

从 SDK 根目录执行，仅编译，不连接或烧录硬件。UART9 只是本板示例分配；按实际接线调整。端口由 SDK 独占，VT03 VideoLink 和裁判端口关闭。查看 `vt12_example_input` 的 online/字段有效位、`vt12_example_custom_info`、`vt12_example_custom` 和 `vt12_example_custom_count`；这些是任务写入的调试副本，不是跨任务一致读取接口。

示例启用 UART TX，但不自动发送。`VT12ExampleSend` 是供应用集成的任务上下文辅助函数，不是 SDK 新 API；调用任务需要预算 Send 的 309 B 包缓冲及 Serial 嵌套栈。通过该函数或直接 `SRNOS_VT02Send`/`Flush` 显式发送，返回成功只表示本地完成，远端仍需核对载荷、序号及 CRC。不要从 ISR、配置回调或会挂起内核的调试表达式中调用。

FIFO 自定义队列深度 4、TTL 1000 ms，每 10 ms 最多取 4 包。需要多订阅者时分别注册路由；需要丢弃/过期统计时使用 `SRNOS_MessageGetStats`。鼠标累计值应按 epoch/有效位做模差，不能把快照当作每周期新增量。

配置、收发合同及错误恢复见 [VT02/VT12 指南](../../Docs/VT02VT12.md)。示例构建不代表具体图传固件或硬件已经通过验收。
