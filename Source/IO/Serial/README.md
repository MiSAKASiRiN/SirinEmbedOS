# Serial 通道与协议接入

`Serial.h` 是公共接口；`serial_internal.h` 只供板级适配和实现使用。HAL handle、DMA、硬件 callback、RX cursor、RS485 DE/RE 均由 `Board/Glue/serial_ports.c` 持有，协议代码只使用通道。

初始化次序为 `SerialConfigure` → 系统协议声明 → 应用 `bind_channels` → `SerialStart`。配置在 Configure 时复制并冻结；通道只能在启动前获取和 Claim。已被其他服务 Claim 或启动后的 `GetChannel` 返回空指针。应用保存自己成功 Claim 的句柄；配置但无所有者的端口阻止启动。句柄生命周期与固件相同，没有热切换或重新绑定。

当前每通道使用 1024 字节 RX ring、256 字节 circular DMA RX、512 字节 DMA TX staging、4 条最大 512 字节 TX 复制队列。RX HT 保持开启，读取实时 NDTR 而非延迟事件携带的旧位置；记录已经被 IDLE 消费的跨圈，防止随后 TC 重复交付。UART IRQ 和 DMA IRQ 在当前 IOC 中优先级不同，cursor 更新与 ring 写入由 FromISR 临界区串行化。超过硬件一圈保留时间的 ISR 延迟无法靠软件恢复丢失字节，需板端压力验证。

所有运行期 I/O 是任务上下文接口。Write 完整复制一包后才报告成功；Flush 等 UART TC，不以 DMA 搬运完成代替线路发送完成。Read 等待输入时释放通道锁，允许正常全双工发送；Transceive 则覆盖排队、发送、TC、响应和超时全过程，防止其他任务插入发送或取走响应。故障恢复使用冻结参数，不改变绑定。

一个公共 Serial worker（AboveNormal、2048 字节栈）处理所有端口的发送与错误恢复；协议解析留在原服务任务。RemoteControl 保留一个 AboveNormal、3072 字节栈的轮询/仲裁线程；Referee 为 High、3072 字节栈。原 Referee TX DMA worker 合入 Serial 共用发送队列，不再额外创建线程。RS485 有 endpoint 时保留一个 AboveNormal、3072 字节栈的总线事务线程；RAW send 仍表示复制入队，空闲时分发被动 RAW 输入。线程栈包含 Picolibc TLS，以上为初始预算，板端需要结合高水位检查。

RS485 固定 UART7，使用硬件 DE，发送期间关闭 RE 抑制本机回显，UART TC 后恢复接收。Modbus 事务匹配地址、功能码、长度和 CRC，广播不等待响应；低速保持至少 3.5 字符，高于 19200 使用至少 1750 微秒静默间隔，当前 1 kHz 内核等待向上取整。队列等待与响应等待分别指定。RAW 回调得到临时数据片段，不能保存指针；超过 256 字节的连续输入会分片。

H12H 的协议实例服务多个舵机 ID，通过 Serial 事务完成命令。当前板级合同仅声明已确认的双线 UART 和 UART7 RS485；没有自动把普通 UART 改为单线的行为。设备适配器和接线需满足实际电气要求。

`bash Tests/Serial/run_host_tests.sh` 覆盖真实核心状态机和编解码，硬件与 RTOS 边界使用 fake。这不能证明实际 UART 极性、DMA 延迟、RS485 DE、USB 或设备时序已经上板通过。
