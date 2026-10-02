# 蜂鸣器驱动

提供频率/音符播放、队列及 ISR 入队 API，硬件采用固定 TIM2/DMA1 Stream0 驱动 GPIOE13。定时器和 DMA 私有 handle、队列及波形实现保留在设备驱动；固定硬件宏由 Board/Support/Devices/buzzer_hardware.h 私有提供。公共头仅提供 SRNOS_BuzzerStatus、SRNOS_BuzzerNote 和 SRNOS_Buzzer* 播放/队列接口，不包含 HAL/RTOS 头；Init、PortHooks、Service 与工作唤醒接口在 buzzer_service.h 私有维护。父项目不初始化硬件，也不手动轮询 Service。队列容量保持，不支持应用覆盖 GPIO/TIM/DMA 布线。

DMA 入口由 Board/Support/Devices/indicators_board.c 转接，启动音和周期服务策略属于 Services/Indicators。主机服务测试与固件编译不代替实板发声检查。

Indicators 读取下一音符截止点；任务/ISR 入队或停止后在临界区外通知工作线程，保留原来的逐音符相对时长及不补跑语义。FromISR 入口只供符合 SDK RTOS 调用条件的中断使用（当前优先级数值 5～15），ISR 不阻塞。主机 Indicators 等待逻辑回归通过；真实音符节奏与 DMA 压力仍待板测。
