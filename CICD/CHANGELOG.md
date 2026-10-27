# Changelog

## Unreleased

- 2026-10-27：基于当前源码分发规则将 CI 工具独立到 CICD，删除 Tests/BringUp/主机编译器依赖，仅构建 Debug/Release 并导出固件、摘要和校验清单；保留父项目模板与可选通知。
- 本地 SDK Debug/Release 配置、编译及 BIN/HEX/size 共 10 步通过；固件 ELF/MAP/BIN/HEX 与 SHA256 清单成功导出。不运行 Tests、不操作硬件；线上调度需推送后核对。
