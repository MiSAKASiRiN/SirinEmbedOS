# Changelog

## Unreleased

- 2026-09-28：统一 SRNOS 公共命名、移除旧全局兼容入口，隔离内核/板级依赖；接口迁移及当前验证边界见 README。本轮主机/编译检查不代表外设或控制行为已完成板测。
- 矩阵改用公开 `SRNOS_MatrixF32` 接口，不再暴露 CMSIS 类型或可替换的 `arm_*` 弱符号；数据所有权、尺寸与重叠限制见 [README](README.md)。
