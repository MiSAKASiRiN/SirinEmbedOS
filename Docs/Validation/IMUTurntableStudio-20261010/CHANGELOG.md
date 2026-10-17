# 验证记录

本文保存对应归档的原始报告。日期、版本、通过范围和结束状态仅对当时现场有效，不表示当前板上状态。当前阅读方法见本目录说明文件。

---

<!-- 原报告：README.md；原文 SHA256：9f21e4bfe636190c571e9d3f0f92d72c1ba0f5f5cfa40317527225c8d109e2e1 -->

# Studio转台校准接入（2026-10-10）

固件revision22 / 配方0x54544332；OmniX Studio插件0.2.0。修改源码位于Examples/IMUTurntableBringUp以及/Users/sirin/PycharmProjects/PythonProject/sirinembedos_plugin。插件已安装项目plugins和/Applications/OmniX Studio.app/Contents/Resources/plugins/moe.sirin.sirinembedos，保留此前插件副本并重新签名，codesign严格检查通过。

固件烧录前revision21三轴零输出；新.text比对一致，MCU运行、调试器detach及GDB Server退出。独立5秒板端采集确认revision22、三轴命令0、fault0；HostLink专用能力响应recipe匹配、job0、phase0。安装后的真实Studio已打开IMU页并连接CDC1，转台按钮启用，任务#0、温度约59.65/60°C、3D模型/INS图正常显示。CDC1当前由Studio持有。未自动点击运动按钮，未写入新校准EEPROM。

验证：INS核心/快照/v1-v2校准持久化/静态校准回归通过；转台空闲零输出、STOP、看门狗；配方遍历、预热门控、断连保持和部分写入状态；插件9项转台拟合/独立验证/坏数据不保存测试、13项协议和19项设备GUI测试通过。理想端点配方测试仅验证状态机，不代表机械仿真或实机动作。

最终FLASH313120B，D1 282912B，ITCM63104B；新增原子统计记录，任务3072字节栈保持，未新增线程。完整多姿态真实运动、标定系数精度、首次本流程EEPROM持久化及重启应用仍待现场执行。完成后保持承重，新系数下次安全重启生效；不使用已失败的固定电压下放。

流程、算法质量门限、记录布局、存储与取消合同见Examples/IMUTurntableBringUp/Calibration.md。
