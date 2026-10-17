# 代码来源与迁移索引

本目录用于追踪来源、许可、导入时的哈希以及旧 API/路径的去向。父项目接入用 [QuickStart](../../QuickStart.md)，升级旧调用用 [SDKMigration](../SDKMigration.md)；不要通过重新运行历史导入配方覆盖当前 SDK。

## 选择记录

| 文件 | 用途 |
|---|---|
| [source-manifest.json](source-manifest.json) | 导入前工作区、各模块来源与原文件 SHA。包含未导入项，不能当作当前文件清单。 |
| [migration-ledger.json](migration-ledger.json) | 模块处理方式、许可状态、源文件与目标关系。 |
| [SDK-SHA256SUMS](SDK-SHA256SUMS) | 台账记录的目标哈希快照；不能代替当前构建产物哈希。 |
| [header-map.json](header-map.json) | 最初公共头路径迁移。 |
| [srnos-renames.json](srnos-renames.json) | 符号、公共头和私有路径重命名。 |
| [board-support-relocations.json](board-support-relocations.json) | 板级文件拆分与重定位，单个源文件可能对应多个目标。 |
| [persistence-relocations.json](persistence-relocations.json) | EEPROM/Storage 重实现去向，不表示旧介质格式兼容。 |
| [system-monitor-relocations.json](system-monitor-relocations.json) | 统计与监控模块映射。 |
| [glue-references.json](glue-references.json) | 板级/USB 外围接入来源。 |

## 查询示例

从 SDK 根目录执行，只读检查来源文件和迁移记录：

```sh
python3 -m json.tool Docs/Migration/source-manifest.json
rg -n 'lib_can|lib_imu|bsp_spi|Board/Support' Docs/Migration/migration-ledger.json Docs/Migration/srnos-renames.json
rg --files Include/SirinEmbedOS Source/IO Board/Support
```

先用台账找到候选目标，再按当前文件和公共头核对。编译不依赖这些映射或 prototype 的绝对路径。`import_prototype.py` 是历史配方；`finalize_ledger.py` 属于台账维护工具，只有明确维护来源记录时才运行，不作为日常构建步骤。

## 许可和历史

保留原版权、许可与上游版本记录；未发现独立许可证不能自行补写开源授权。SDK 不复制嵌套 `.git` 或旧 Framework 子模块结构。上游库来源及适配说明见 ThirdParty 各目录。

原逐模块迁入结果、接线调整与阶段性状态完整保存在 [CHANGELOG](CHANGELOG.md)。旧优先级、旧路径和历史验证结果只适用于记录当时；当前合同查询模块 README。
