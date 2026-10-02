# 数学与滤波

公开 C++ 接口位于 `SirinEmbedOS/Algorithms/Math/`，使用 `SRNOS::Math` 命名空间，提供角度、信号处理、OLS 和定容 Kalman。公共头只依赖 SDK 与标准库。

`Matrix.h` 提供 C11/C++17 均可调用的 `SRNOS_MatrixF32`、绑定、乘法和转置。矩阵为 row-major，调用者持有数据；绑定不分配、不清零，乘法/转置输出必须尺寸匹配且不与输入重叠。当前沿用原三函数的计算循环，返回 `SRNOS_MATRIX_OK` 或尺寸错误，不再公开 CMSIS 类型或可被链接替换的 `arm_*` 弱符号。

验证命令：`sh Tests/Algorithms/Math/run_host_tests.sh`，覆盖矩阵乘法、转置、错误尺寸及 Kalman/OLS/信号算法。主机结果不能证明控制效果或板端耗时。
