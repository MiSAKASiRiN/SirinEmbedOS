# 验证记录

本文保存对应归档的原始报告。日期、版本、通过范围和结束状态仅对当时现场有效，不表示当前板上状态。当前阅读方法见本目录说明文件。

---

<!-- 原报告：README.md；原文 SHA256：7d39d50c8243507607dc540080a5d021b9a89bb1c6aedff5fd683ccfb834314b -->

# 2026-10-10 慢速重复实测

使用原revision12固件（build/IMUTurntable27/board/run12.elf），板上.text比对一致后复位执行，没有修改控制代码、编译、烧录或运行本机测试。

ID1速度目标54°/s，15秒运行、3秒零指令间隔，共三次；每次重新启动清零PI，ID2/3始终零输出。run13采集56.923秒，正常complete_zero结束。GDB读回completed_segments=3、stalled_segments_mask=0、faultNone、inhibit0、三轴command0，说明三段均完整结束且无堵转。

ID1采集指令范围-3197～550，温度反馈全程23°C；ID2/3全部已采集指令0。双IMU各56919样本，无序号间断和解码错误。detach后CDC复核phase24、fault0与三轴零指令。探针和CDC已释放；复位将再次执行三次慢速。

完整数据保存在build/IMUTurntable27/board/run13*。这次没有复现此前停滞，不代表根因已定位。
