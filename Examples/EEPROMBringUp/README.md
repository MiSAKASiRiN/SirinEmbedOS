# EEPROMBringUp

主机备份恢复专用 `command=3`：仅在 stage=1 时暂停，先 dump 原始备份，再把恰好 16384 B 的主机镜像上传到 `srnos_eeprom_work` 指向的区域；设置 `upload_hash` 为全片 FNV-1a32（初值 2166136261、逐字节 xor 后乘 16777619），`upload_arm=0x53524E56`，最后设置 command=3 并继续。固件失效上传区 D-cache、核对 arm/hash 后才写入，随后整片读回逐字节核对。stage=8 为恢复成功，94 为未授权/上传校验失败（不写），91 为写入或读回失败。此路径不运行测试图样或 SCL 忙等，必须由操作者明确触发；复位仍只备份。SWD 写入前保持 stage=1，上传和命令必须在同一次暂停中完成。

FT24C128A 器件级一次性验证应用；默认加热、INS 融合、参数加载和 HostLink 存储关闭，环境传感器继续使用共享 I2C4。默认示例仍为 Minimal。

```sh
cmake -S . -B build/EEPROMBringUp -G Ninja -DCMAKE_TOOLCHAIN_FILE=CMake/starm-clang.cmake -DCMAKE_BUILD_TYPE=Debug -DSIRINEMBEDOS_EXAMPLE=EEPROMBringUp
cmake --build build/EEPROMBringUp
```

应用先读取 16 KiB 到 `srnos_eeprom_backup` 指向的缓冲区（现在为指针，GDB dump 范围使用该指针到 `+16384`，不要使用指针变量自身地址），停留在 `srnos_eeprom_bringup.stage=1`；主机必须先将备份 dump 为本地文件，再选择 `command=1`（测试并恢复）或已明确允许破坏内容时 `command=2`（保留测试数据）。复位不会自动开始写入。测试会覆盖整片，写前必须取得对应授权并保管外部备份。

验证依次为整片阻塞写/异步读、整片异步写/阻塞读、非对齐页边界、`0xFF/0x100`、高地址、末字节和非法范围。所有写入本身执行读回校验；command=1 时成功或失败后都尝试恢复原始备份并再次整片读取逐字节比对；command=2 时跳过恢复，最后读取整片并发布 `final_hash` 和工作区供主机独立核对。`stage=6` 表示全部通过且恢复成功；stage=7 表示测试通过且按要求保留测试内容；90=初始备份失败，91=测试或恢复失败，92=工作缓冲分配失败，93=数字 SCL 观测未完成。后两者不能当作 EEPROM 已恢复。

例程通过已有私有测试适配一次分配 32 KiB heap，分别用于 16 KiB 备份及 16 KiB 工作区，保留到主机完成取证；生产驱动本身不做堆分配。完成后比较 `srnos_eeprom_work` 与主机保存的备份，并记录共享总线/传感器快照。调试暂停会扰动传感器时序，采样窗口中不设断点、不暂停。

命令/状态放入现有非缓存 D2 Debug Watch，缓存 RAM 中的备份与恢复工作区在发布完成前显式 clean D-cache，保证主机 SWD dump 与 CPU 看到的数据一致；没有修改链接脚本或 MPU。

实测见 [验证记录](../../Docs/Validation/EEPROM-20260928/README.md)。恢复失败时保留备份、停止进一步测试，不继续刷写其他固件。

共享总线验证增加一个仅此例程的 Normal / 2 KiB 观测线程，每 1 ms 读取 LIS/SPA 状态，记录各阶段的样本计数、最大样本间隔及非 READY 观测。收到 command 后先等待 2 秒消除主机暂停影响，再采集 3 秒空闲基线。`srnos_eeprom_continuity[1..8]` 分别为基线、首次写/读、第二次写/读、边界、恢复写/读，每项第二维为 LIS/SPA；阶段 0 表示不记录。该线程不访问硬件总线，生产 SDK 不增加线程。见 [I2C 并发记录](../../Docs/Validation/I2C-20260928/README.md)。

收到命令后，在基线之前做一次只读的数字 SCL 观测：PB8 保持 I2C 复用模式，以 GPIO IDR 与 DWT CYCCNT 记录 128 个相邻上升沿间隔，保存在 `srnos_i2c_scl`。中断可能造成漏采边沿，应看分布/中位数，不将长间隔直接解释为时钟变慢；该方法不测模拟上升时间。只执行一次，不计入连续性窗口。command=2 的阶段 7 无记录，阶段 8 为最终读取，`restored=false` 是预期结果。复位仍只读备份并等待命令，不会自动循环擦写。

400 kHz Fast-mode 的 command=2 实测 21 项通过，整片写含校验约 5.12 秒、读约 1.03 秒，最终全片内容主机核对一致；见 [提速记录](../../Docs/Validation/I2C400-20260928/README.md)。该固件关闭融合/加热，不代替正式 INS 应用验收。

## 托管接口迁移

观察线程改为托管周期任务，应用延时使用 SRNOS_Delay；内核堆专项压力隔离在 kernel_test_adapter，仅此验证应用编入。 旧 ThreadConfig.entry 不再支持；公共创建/停止/删除及同步用法见 Source/Kernel/README.md。本次编译不意味着重新执行板端测试。

原生堆压力、DWT/GPIO SCL 短测和 SWD 缓冲区 cache 发布均隔离在 `kernel_test_adapter.c`，以单独的私有 OBJECT 编译。`app_main.c` 只使用 SDK 及测试自有类型，不包含 HAL/FreeRTOS，也不获得其 include 路径；SCL 测试仍是显式调试命令触发的测量，不是生产任务。

本例显式设置 `storage.enabled=false`，允许整片维护；生产默认管理区不可原始写入。改为测试 heap 备份后须在下一次实测确认 32 KiB 分配及传感器共存，旧板测仅说明旧测试镜像。
