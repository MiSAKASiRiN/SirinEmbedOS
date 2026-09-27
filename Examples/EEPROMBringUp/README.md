# EEPROMBringUp

FT24C128A 器件级一次性验证应用；默认加热、INS 融合、参数加载和 HostLink 存储关闭，环境传感器继续使用共享 I2C4。默认示例仍为 Minimal。

```sh
cmake -S . -B build/EEPROMBringUp -G Ninja -DCMAKE_TOOLCHAIN_FILE=CMake/starm-clang.cmake -DCMAKE_BUILD_TYPE=Debug -DSIRINEMBEDOS_EXAMPLE=EEPROMBringUp
cmake --build build/EEPROMBringUp
```

应用先读取 16 KiB 到 `srnos_eeprom_backup`，停留在 `srnos_eeprom_bringup.stage=1`；主机必须先将备份 dump 为本地文件，再将 `command=1`。复位不会自动开始写入。测试会覆盖整片，写前必须取得对应授权并保管外部备份。

验证依次为整片阻塞写/异步读、整片异步写/阻塞读、非对齐页边界、`0xFF/0x100`、高地址、末字节和非法范围。所有写入本身执行读回校验；成功或失败后都尝试恢复原始备份并再次整片读取逐字节比对。`stage=6` 表示全部通过且恢复成功；90=初始备份失败，91=测试或恢复失败，92=工作缓冲分配失败。后两者不能当作 EEPROM 已恢复。

例程独占 16 KiB 静态备份及 16 KiB 测试 heap 工作区，保留到主机完成取证；生产驱动本身不做堆分配。完成后比较 `srnos_eeprom_work` 与主机保存的备份，并记录共享总线/传感器快照。调试暂停会扰动传感器时序，采样窗口中不设断点、不暂停。

命令/状态放入现有非缓存 D2 Debug Watch，缓存 RAM 中的备份与恢复工作区在发布完成前显式 clean D-cache，保证主机 SWD dump 与 CPU 看到的数据一致；没有修改链接脚本或 MPU。

实测见 [验证记录](../../Docs/Validation/EEPROM-20260928/README.md)。恢复失败时保留备份、停止进一步测试，不继续刷写其他固件。

共享总线验证增加一个仅此例程的 Normal / 2 KiB 观测线程，每 1 ms 读取 LIS/SPA 状态，记录各阶段的样本计数、最大样本间隔及非 READY 观测。收到 command 后先等待 2 秒消除主机暂停影响，再采集 3 秒空闲基线。`srnos_eeprom_continuity[1..8]` 分别为基线、首次写/读、第二次写/读、边界、恢复写/读，每项第二维为 LIS/SPA；阶段 0 表示不记录。该线程不访问硬件总线，生产 SDK 不增加线程。见 [I2C 并发记录](../../Docs/Validation/I2C-20260928/README.md)。
