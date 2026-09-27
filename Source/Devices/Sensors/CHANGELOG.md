# Changelog

## Unreleased · 2026-09-28

- LIS/SPA 改用 I2C4 分发队列的保留槽和 30 ms 完成等待，支持 EEPROM 大量分段读写时继续周期采集。
- 保留环境线程数量、Normal 优先级、2 KiB 栈、10 ms 默认轮询及器件配置；新增模块说明和并发板测记录。
