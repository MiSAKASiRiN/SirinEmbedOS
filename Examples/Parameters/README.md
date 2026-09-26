# 业务参数示例

把 `app_main.cpp` 作为父项目固件的应用源文件，替代 `Examples/Minimal/app_main.c`，链接 `SirinEmbedOS::Platform` 即可。

启动前静态注册三个有上下界且显式 `PARAMS_MUTABLE_BUSINESS` 的PID增益；SDK冻结注册表后，通过USB CDC1 HostLink CONFIG服务读取/写入这些数值。默认关闭EEPROM自动加载，可显式SAVE/LOAD。系统架构配置没有进入参数表，主机不能用此服务改变UART用途、CAN模式、IMU选择或服务启用状态。

Application线程每10ms取得一个三值一致快照。示例不保存实际控制器指针，不驱动执行器；真实应用在自己的控制周期边界把本地快照交给算法。参数对象和名称均为固件寿命，不能把栈上临时对象注册后离开作用域。
