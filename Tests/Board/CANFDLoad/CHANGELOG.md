# Changelog

## Unreleased

- 2026-10-06：六镜像实板执行与固定ARM短枚举布局解码完成；54窗口计数核对、356功能帧及两个60秒目标负载通过，过载拒绝/TTL与接纳后交付差额一致。新增离线summary复查，记录动态栈/IMU与身份记录/LIS限制，完整Flash恢复后CDC监测通过。见 [证据](../../../Docs/Validation/CANFD-Board-20261006/README.md)。

- 2026-10-05：新增六种初始化模式的CAN1/CAN3板测，全部合法DLC/标准扩展ID/双向全payload校验，1ms生产者阶梯及60秒目标负载。记录硬件、router、CPU、INS/传感器和窗口外栈水位；实际板测结果另归档，不以编译代表通信通过。
