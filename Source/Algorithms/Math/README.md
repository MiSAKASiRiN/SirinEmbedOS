# 数学与滤波

当前 Matrix API 由自维护 `Matrix.c` 实现，不依赖 CMSIS-DSP；仓库保留的 CMSIS-DSP 快照不自动成为应用的公共依赖。

公开 C++ 接口位于 `SirinEmbedOS/Algorithms/Math/`，使用 `SRNOS::Math` 命名空间，提供角度、信号处理、OLS 和定容 Kalman。公共头只依赖 SDK 与标准库。

`Matrix.h` 提供 C11/C++17 均可调用的矩阵绑定、乘法和转置。存储为 row-major，由调用者持有；绑定不分配、不清零，输出必须尺寸匹配且不能与输入重叠。错误尺寸返回 `SRNOS_MATRIX_SIZE_MISMATCH`。

```c
#include "SirinEmbedOS/Algorithms/Math/Matrix.h"

SRNOS_MatrixStatus multiply_example(float out[2]) {
    float a_data[4] = {1, 2, 3, 4};
    float b_data[2] = {5, 6};
    SRNOS_MatrixF32 a, b, result;
    SRNOS_MatrixInitF32(&a, 2, 2, a_data);
    SRNOS_MatrixInitF32(&b, 2, 1, b_data);
    SRNOS_MatrixInitF32(&result, 2, 1, out);
    return SRNOS_MatrixMultiplyF32(&a, &b, &result); // OK: out = {17, 39}
}
```

算法精度要求和控制效果由应用验证。
