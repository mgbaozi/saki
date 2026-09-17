# Saki

Saki 是一块放在桌面上的 AI Agent 状态屏。它由 ESP32-S3 设备和 macOS 伴随程序组成，
把 Codex 和 Claude Code 的工作状态同步到独立屏幕，让你不切换窗口也能知道任务正在运行、
等待操作，还是已经结束。

当前稳定版本为 [0.5.0](https://github.com/mgbaozi/saki/releases/tag/v0.5.0)。目标硬件为
正点原子 ATK-DNESP32S3B3 / ESP32S3 BOX3，16 MiB Flash、8 MiB Octal PSRAM、320×240 横屏。

## 实机效果

| 正在思考 | 任务完成 |
| --- | --- |
| ![Saki 正在思考状态屏幕特写](./docs/images/saki-screen-thinking.png) | ![Saki 任务完成状态屏幕特写](./docs/images/saki-screen-completed.png) |

## 主要能力

- 在独立屏幕上实时查看 Codex / Claude Code 的任务状态、执行时间、进度和中英文标题。
- 同时关注多个 Session：最近会话显示在主屏和侧栏，触摸即可切换查看。
- 及时看到等待输入、等待批准、完成和失败等关键节点，离开 Agent 窗口也不漏掉需要处理的任务。

## Quick Start

以下流程使用已经发布的 0.5.0 release 固件，不需要安装 ESP-IDF，也不需要先构建固件。
需要 macOS、Python 3.12、目标开发板和一根支持数据传输的 USB 线。

### 1. 下载并校验 release 固件

```zsh
curl -LO https://github.com/mgbaozi/saki/releases/download/v0.5.0/saki-0.5.0.zip
curl -LO https://github.com/mgbaozi/saki/releases/download/v0.5.0/saki-0.5.0.zip.sha256
shasum -a 256 -c saki-0.5.0.zip.sha256
unzip saki-0.5.0.zip
cd saki-0.5.0
```

### 2. 烧录设备

先在一个临时虚拟环境中安装烧录工具：

```zsh
python3.12 -m venv .venv
.venv/bin/pip install 'esptool==4.12.0'
```

按住开发板 `K0`，短按 `RST`，等待约一秒后松开 `K0`。用下面的命令找出新出现的串口：

```zsh
find /dev -maxdepth 1 -name 'cu.usb*' -print
```

将实际端口替换到命令中，直接从 `0x0` 烧录 release 合并镜像：

```zsh
.venv/bin/python -m esptool --chip esp32s3 \
  --port /dev/cu.usbmodemXXXXXX --baud 460800 \
  write_flash 0x0 saki-0.5.0-full.bin
```

烧录完成后，不按 `K0`，短按一次 `RST` 启动应用。端口名会随重新枚举变化，每次操作都应
重新确认，不要把真实端口永久写入配置。

### 3. 安装 Mac Host

回到准备存放源码的目录，安装 USB 模式所需的 Host：

```zsh
git clone --branch v0.5.0 --depth 1 https://github.com/mgbaozi/saki.git
cd saki
python3.12 -m venv host/.venv
host/.venv/bin/pip install -e host

host/.venv/bin/saki-host serial list
host/.venv/bin/saki-host doctor
```

### 4. 接入 Agent 并启动常驻服务

按实际使用的 Agent 安装一个或两个 hook：

```zsh
host/.venv/bin/saki-host hooks install --source codex
host/.venv/bin/saki-host hooks install --source claude_code

host/.venv/bin/saki-host hooks check --source codex
host/.venv/bin/saki-host hooks check --source claude_code
```

然后安装用户级常驻服务：

```zsh
scripts/saki-service.zsh install
scripts/saki-service.zsh status
```

重新打开 Codex 或 Claude Code 会话并提交一个任务，状态就会同步到屏幕。hook 的作用域、
卸载和故障排查见 [0.5.0 用户指南](./docs/versions/0.5.0/USER_GUIDE.md)；BLE 配对与诊断见
[Host 开发说明](./host/README.md)。

## 工作方式

```text
Codex / Claude Code lifecycle hooks
        │  本地 Unix Domain Socket
        ▼
saki-host on macOS ── USB CDC / BLE GATT + NDJSON ── ESP32-S3 firmware ── LCD
```

Mac Host 负责采集事件、提取并过滤显示信息、合并多 Session 状态和恢复连接；设备固件负责
协议校验、计时、动画、触摸和界面渲染。Host 不会把完整对话、隐藏思维链或原始工具输出
发送到设备。协议只传输与角色无关的语义状态，后续主题或角色可以完全由设备端实现。

## 开发教程

从源码构建需要 Python 3.12 和 ESP-IDF 5.5.3。构建脚本默认从仓库布局推导 ESP-IDF 路径；
如果安装在其他位置，设置 `SAKI_IDF_PATH`。

```zsh
git clone https://github.com/mgbaozi/saki.git
cd saki

python3.12 -m venv host/.venv
host/.venv/bin/pip install -e 'host[dev]'
export SAKI_IDF_PATH=/path/to/esp-idf-v5.5.3

scripts/check.zsh
scripts/build-firmware-profile.zsh dev
```

构建不需要连接设备。进入 ROM 下载模式并重新确认端口后，烧录开发固件：

```zsh
scripts/flash-firmware.zsh /dev/cu.usbmodemXXXXXX dev
```

需要开发或验证 BLE 时安装可选依赖；release profile 和 Unity 测试镜像也使用独立构建目录：

```zsh
host/.venv/bin/pip install -e 'host[dev,ble]'
scripts/build-firmware-profile.zsh release
scripts/build-firmware-tests.zsh
```

更多构建、真机测试和独占串口注意事项见 [固件开发说明](./firmware/README.md) 和
[Host 开发说明](./host/README.md)。

## 常用命令

```zsh
# 查看或维护常驻服务
scripts/saki-service.zsh status
scripts/saki-service.zsh logs
scripts/saki-service.zsh restart
scripts/saki-service.zsh uninstall

# 查看或清理 Host 保存的显示记录
host/.venv/bin/saki-host sessions list
host/.venv/bin/saki-host sessions forget all
```

需要独占 USB 或 BLE 进行手工发送、回放、fuzz 或 soak 时，先停止常驻 Host，完成后再启动。

## 项目结构

- `firmware/`：ESP-IDF + LVGL 设备固件。
- `host/`：运行在 macOS 上的 Python 伴随程序。
- `protocol/`：Host 与固件共享的 NDJSON Schema 和脱敏测试 fixture。
- `scripts/`：环境、构建、烧录、测试和 LaunchAgent 管理入口。
- `docs/`：规格、工程设计、任务记录、发布说明和路线图。

## 文档

- [文档索引](./docs/README.md)
- [0.5.0 产品规格](./docs/versions/0.5.0/SPEC.md)
- [0.5.0 工程实施](./docs/versions/0.5.0/IMPLEMENTATION.md)
- [0.5.0 验证记录](./docs/versions/0.5.0/TASKS.md)
- [0.5.0 用户指南](./docs/versions/0.5.0/USER_GUIDE.md)
- [0.5.0 发布说明](./docs/releases/0.5.0.md)
- [后续路线图](./docs/ROADMAP.md)

## 许可证与项目名称

Saki 的原创代码和文档使用 [Apache License 2.0](./LICENSE)，归属及第三方声明见
[NOTICE](./NOTICE)。第三方组件继续适用各自的许可证和版权声明。

可以依照许可证使用、修改和分发本项目及衍生作品，但衍生项目、产品、服务或发行版不得
继续以 `Saki` 命名，也不得暗示获得原项目或作者背书。为说明代码来源和保留归属信息，
仍可合理引用 “Saki”；分发时必须保留适用的 `LICENSE`、`NOTICE`、原项目和作者信息。
