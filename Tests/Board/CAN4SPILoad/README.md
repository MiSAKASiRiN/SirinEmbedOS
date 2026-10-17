# CAN1 / CAN4 共享 SPI 与 IMU 板测

用户确认CAN1/CAN4短接后使用。独立Classic1M、FD1M无BRS、FD仲裁1M/数据2、4、5、8M有BRS镜像；CAN2/3空载。沿用CANFDLoad帧格式/序号/全payload校验与Idle CPU、栈测量，新增MCP寄存器/驱动统计、BMI加速度/BMI陀螺/LSM/MCP四组SPI事务统计及IMU逐样本间隔。每镜像等待INS有效，保留正式温控/背景服务，不写EEPROM。

Classic正式镜像的11个窗口为空载5s、两个方向单向3k各10s、双向各1k/2k各10s、双向各3k三轮60s、过载各6k 10s、恢复各1k 10s、最后双向各3k 600s。`ClassicQuick` 将目标档改为10s，仅用于定位。FD包含全部合法DLC与STD/EXT、单向/双向阶梯、过载恢复；8M64B各3k维持60s以表征上限，不将该档作为 Classic 硬验收。

`SRNOS_TestCANPerf` 为额外8564B/version=2（解码仍兼容8388B/version=1）静态结果：每个十秒窗口CPU、CAN4唤醒/轮次数、各任务Monitor CPU平均、MCP不确定提交次数与SPI总DMA启动尝试/完成数。SPI每帧事务数由物理completed除以已处理RX/TX计算，包含空状态检查。`SRNOS_TEST_DETAIL_TIMING=ON` 仅用于独立短定位构建，启用CAN4每帧处理计时；正式验收关闭。详细定位构建的ClassicQuick每档缩短为1秒，仍等待INS有效。生产者仍按1ms真实调度，无补发追赶或主动降速。

SPI前后快照围绕活动窗口，直方图是增量，max_queue_us/max_transfer_us为启动以来峰值，不能当作本档新峰值。IMU observer记录本窗口样本数、完成时间间隔和采集耗时峰值；频率按实际elapsed_us判断（相位容差1样本），过载下控制线程晚结束造成的额外样本不能误判缺样；CAN生产周期仍按请求时长判定，不放宽生产不足。加入的少量原子统计开销属于测试负载。驱动错误/硬件CAN计数在500ms排空后读取，MCP读取委托给唯一CAN4服务线程，仅在窗口外等待结果。Classic rx_overflow是可覆盖的调试日志环，不等同于硬件RX或应用回调丢帧。

栈扫描只在全部窗口结束后执行；测量中不暂停GDB，不读取CDC。准备前备份完整Flash，每镜像下载验证；结束必须恢复原1MiB并读回比对、复位运行、清理自己的断点与调试连接。

```sh
cmake -S Tests/Board/CAN4SPILoad -B build/CAN4SPILoad -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$PWD/CMake/starm-clang.cmake" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/CAN4SPILoad -j8
```

实际硬件结果另归档；编译不是吞吐/IMU期限验收。串口、主动断线/故障恢复与四路并发未覆盖；8M错误配置下观测到的Bus-Off单独记录，不作为故障恢复验收。

`FD8MProbe` 跳过INS预热，只跑64个格式用例，不用于IMU或负载验收。生产构建使用当前板级时序；历史 Aligned 专用 wrap 证据仅用于对应镜像对照。结果主结构维持26816B；IMU超阈计数现用1200µs，旧归档的1500µs口径不可混用。

`decode.py <result.bin> ...`生成完整 JSON 和摘要。`achieved`要求接纳帧全部交付、无拒绝/缺序/损坏并验证生产周期与实际速率；周期不足即使没有背压也不算达标。接纳减接收再减软件TTL的差额仅标为未被TTL解释的丢失，须结合硬件RX overflow等证据判断，不能把overflow事件数当成丢帧数。SPI最大耗时是启动以来峰值，直方图才是窗口增量；外设最后状态和事件位不作算术增量。

`summarize.py <含摘要的目录>`生成负载表、总摘要与IMU/SPI观测汇总；逐帧一致性检查通过不代表每档吞吐通过。

额外性能结果使用 `decode_performance.py <name-perf.bin>`，旁边应有同名 `<name.bin>` 主结果。它逐档检查CAN、CPU十秒窗口与IMU保护线，保留未达标档。少于10秒的档位没有完整CPU窗口，cpu_pass为null（未判定），不标成失败。

## 结果判定

正式结果解码后，运行 `python3 Tests/Board/CAN4SPILoad/check_acceptance.py <结果目录>` 检查固定命名classic-final的3×60s＋600s、78个CPU窗口、端到端交付与全部阶段IMU保护线；失败返回非零。

中间版本、实际负载结果与测量限制见 [CHANGELOG](CHANGELOG.md)。
