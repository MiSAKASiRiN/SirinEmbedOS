# Changelog

## Unreleased

- 公共命名统一 SRNOS_BuzzerStatus/Note 与 SRNOS_Buzzer*；移除公共 HAL 依赖，初始化/hooks/Service 收入私有头。增加队列变更唤醒与下一截止点查询，服务主机测试通过；实际发声待板测。

- 2026-09-28：完成 Support 职责整理；接口/时序/调度保持，固定硬件配置改为板级私有。21 组主机测试与固件集成构建通过；未板测。

## 历史验证补充

Indicators 读取下一音符截止点；任务/ISR 入队或停止后在临界区外通知工作线程，保留原来的逐音符相对时长及不补跑语义。FromISR 入口只供符合 SDK RTOS 调用条件的中断使用（当前优先级数值 5～15），ISR 不阻塞。主机 Indicators 等待逻辑回归通过；真实音符节奏与 DMA 压力仍待板测。
