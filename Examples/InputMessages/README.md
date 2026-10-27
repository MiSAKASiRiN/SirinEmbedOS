# InputMessages

此示例启用 DR16、VT03/13、W.Bus 和四种传输上的 SirinRMT，共七个独立来源。应用每 10 ms 读快照，并消费 service 0x40/type 1 的自定义消息；不注册电机，不执行控制动作。端口为示例分配：DR16 UART8、VT03 UART9、W.Bus USART6（RX 反相）、消息 UART USART1；HostComm 使用 USB，UDP 消息端口5001，地址沿用默认192.168.1.50/24。根据实际接线调整后再运行。

```sh
cmake -S . -B build/InputMessages -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=CMake/starm-clang.cmake \
  -DCMAKE_BUILD_TYPE=Release -DSIRINEMBEDOS_EXAMPLE=InputMessages
cmake --build build/InputMessages -j8
```

查看 `srnos_example_inputs` 中的 coherent 副本和 `srnos_example_messages`。数组顺序由 app_main.c 的 source_names 定义。online 与字段 valid 都需要检查；只有 RF206S CH5 在示例中带有实测归一化校准，其余 W.Bus 通道保留 raw。

注册顺序是：应用配置 → Serial/内置来源绑定 → bind_channels 注册自定义路由 → 冻结 → HostComm 注册 → 内核初始化/一次分配 → worker 启动。应用不能在注册阶段做 I/O 或创建线程。板端是否收到实际无线/串口/以太网数据需要单独验证；构建和主机注入测试不证明硬件链路可用。

接口说明：[Input 与 SirinRMT](../../Source/Services/Input/README.md)、[Message](../../Source/Services/Message/README.md)。
