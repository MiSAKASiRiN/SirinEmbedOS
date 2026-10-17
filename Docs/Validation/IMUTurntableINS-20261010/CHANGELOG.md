# 验证记录

本文保存对应归档的原始报告。日期、版本、通过范围和结束状态仅对当时现场有效，不表示当前板上状态。当前阅读方法见本目录说明文件。

---

<!-- 原报告：README.md；原文 SHA256：93dcd1f144c000997f095c283c4537855b5964be19bbd53e4723a4bec1ef1d54 -->

# 转台上位机开发模式 / revision21

按用户要求启用正式INS和60°C温控，MotorControlEnabled=false暂时旁路三轴控制；原控制器全部保留，开关仅编译期修改。CAN3 ID1/2/3周期命令始终0，反馈和32项原有遥测保留；STOP不触发旧定时下放。INS由SDK自动注册原始IMU/姿态/环境流，移除示例重复注册，避免注册BUSY。未改变SDK公共API、生成边界或应用线程预算。

增量固件编译、三轴旁路/STOP/时序归零针对性检查通过。用户授权烧录；烧录前旧版处于故障5（记录为ID1历史温度137）、三轴零输出、ID3 raw4046；当时即时温度25/26/25°C，未据此改写旧故障原因。新固件.text比对通过，MCU运行且已detach。

100秒采集：BMI/LSM各99996条测量记录；INS约上电40.5秒开始输出，60416条测量记录，序号连续、1000Hz、无无效样本/解码错误。末尾独立电机采集5秒确认revision21、phase0、fault0、三个输出0；unwrapped/speed控制器派生字段标记无效，原始编码器等仍有效。

末次状态INS phase6降级运行、flags529（VALID+THERMAL_PENDING+RELATIVE_HEADING）、两颗IMU健康。LSM反馈61.449°C、目标60°C、PWM43.66%，温控开启但尚未达到温稳判据；不能宣称恒温已稳定。不因THERMAL_PENDING丢弃VALID姿态，上位机应展示质量标志。四元数wxyz为机器人到世界，Euler单位rad，相对航向无绝对北向。

上位机使用CDC1 HostLink V2，订阅TELEMETRY service4/message1：正文小端<HIB>，0x0102、1000、1；service4/message9空正文读取INS状态。108字节姿态记录与批头见Docs/HostLink.md。新鲜订阅才输出，温稳/初始化期间可查状态。

已释放CDC1供上位机连接。原始CSV/二进制留在build/IMUTurntable27/board/run23-ins，固件run23.elf。附元数据、状态、电机记录与烧录日志，不把构建目录原始采集自动纳入Git。
