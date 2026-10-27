# CI/CD

根 `.gitlab-ci.yml` 引入本目录的 `sdk.gitlab-ci.yml`。推送分支、标签或创建 MR 时构建 SDK Minimal 的 Debug/Release，导出 ELF、MAP、BIN、HEX、SHA256 清单与构建摘要。不依赖或分发 Tests、BringUp 和原始验证归档，不运行测试套件。

## Runner 与变量

使用已有 Linux shell Runner，安装 Python 3.10+、CMake 3.22+、Ninja 和完整 ST Arm Clang 21.1.1 / Picolibc 工具链（含 starm-objcopy、starm-size）。

| 变量 | 用途 |
|---|---|
| `SRNOS_CI_RUNNER_TAG` | 必填，现有 Runner 的实际 tag |
| `SRNOS_CI_ENV_FILE` | 可选，Runner 上可信环境脚本的绝对路径，用于设置工具链 PATH |
| `SRNOS_CI_JOBS` | 并行构建数，默认 8 |
| `FEISHU_WEBHOOK` | 可选，Masked/Protected 机器人地址，关闭变量展开 |
| `FEISHU_SIGNING_SECRET` | 可选，Masked/Protected 签名密钥 |

通知仅在受保护 ref 且已配置 Webhook 时执行。通知失败不覆盖构建结果；未配置时不创建通知任务。地址和密钥不写入源码。日志脱敏不替代 Runner 日志和 artifact 的访问控制。

## 本地使用

```sh
python3 CICD/run.py --mode sdk --source . --sdk . --target SirinEmbedOS
```

每次使用独立临时构建目录，不覆盖调试产物。`ci-output/` 只收集构建日志、摘要、固件和校验清单；失败也上传诊断，重新运行清理旧产物。GitLab 产物声明保留 7 天，最新成功产物可能由实例策略延长保留。

## 父项目

将 `parent.gitlab-ci.yml` 复制为父仓库根流水线，设置 `SIRINEMBEDOS_PATH` 和 `CI_FIRMWARE_TARGET`。父项目调用 SDK 的 `CICD/run.py --mode application`，只构建自己的 Debug/Release 固件，不构建 Minimal。父仓库自行配置 SDK 子模块的 CI_JOB_TOKEN 访问许可，子模块固定 commit，不追踪最新分支。GitLab 在 checkout 前解析配置，不能通过 local include 引入子模块文件。

```sh
python3 SirinEmbedOS/CICD/run.py --mode application --source . --sdk SirinEmbedOS --target MyRobot
```

CD 范围为产物交付和可选结果通知；不烧录、不部署机器人，也不自动创建 Release。构建成功不表示硬件验收通过。
