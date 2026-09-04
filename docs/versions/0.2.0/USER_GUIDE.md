# Saki 0.2.0 用户指南

> 状态：Release candidate
> 适用硬件：正点原子 ATK-DNESP32S3B3 / ESP32S3 BOX3（16 MB Flash）
> 第一版传输：USB CDC ACM

## 1. 系统组成

Saki 由两部分组成：

- ESP32 固件负责屏幕、动画、本地执行时间、USB 协议和断线提示；
- Mac Host 接收 Codex 生命周期 hooks，执行脱敏和状态映射，再通过 USB 串口发送完整快照。

第一版必须保持 USB 连接且 Mac Host 正在运行。BLE 和 Wi-Fi fallback 不属于 0.2.0。

## 2. 屏幕与触摸

- 屏幕已亮时，点击任务/动作卡可在摘要和详情之间切换；详情会在 15 秒后自动返回。
- 屏幕已变暗时，第一次点击只恢复正常亮度，不会同时展开详情；再点击任务/动作卡才会展开。
- 空闲 5 分钟后视觉亮度降至 35%，断线 1 分钟后降至 20%；新状态到达时恢复 80%。
- 目标板只提供背光开关，三档亮度由屏幕内容遮罩实现，不会降低背光电功耗。
- 触摸仅控制本地显示，不会批准、取消、重试或以其他方式操作 Agent。

## 3. 烧录设备固件

先解压 `saki-0.2.0.zip`，并在解压目录校验文件：

```zsh
shasum -a 256 -c SHA256SUMS
```

进入 ROM 下载模式：

1. 按住 `K0`；
2. 短按 `RST`；
3. 等待约 1 秒；
4. 松开 `K0`。

每次都要重新发现端口，不能保存上一次的 `/dev/cu.usbmodem*` 路径：

```zsh
find /dev -maxdepth 1 -name 'cu.usb*' -print
```

使用合并镜像烧录：

```zsh
esptool.py --chip esp32s3 \
  --port /dev/cu.usbmodemXXXXXX \
  --baud 460800 \
  write_flash 0x0 saki-0.2.0-full.bin
```

烧录结束后不要按 `K0`，只短按一次 `RST`。设备会以产品名 `Saki Agent Display`
重新枚举，应用端口通常与 ROM 下载端口不同。

## 4. 安装 Mac Host

开发仓库方式要求 macOS 上已有 Python 3.12：

```zsh
cd /path/to/saki
python3.12 -m venv host/.venv
host/.venv/bin/python -m pip install -e host
host/.venv/bin/saki-host doctor
```

正常使用推荐安装用户级 LaunchAgent：

```zsh
scripts/saki-service.zsh install
scripts/saki-service.zsh status
scripts/saki-service.zsh logs
```

服务会在登录后启动，并在异常退出后由 `launchd` 拉起。项目目录或 `host/.venv` 的位置
改变后，必须重新执行 `install`，因为 LaunchAgent 使用安装时解析出的绝对路径。

## 5. 启用 Codex hooks

项目 hooks 位于 `.codex/hooks.json`。Codex 首次加载或 hooks 内容改变时会请求信任；只有
确认信任后，Agent 事件才会送到 Host。hook 进程通过权限为 `0600` 的 Unix Domain Socket
投递快照，不直接打开串口；配置通过 `scripts/saki-hook.zsh` 从当前 checkout 定位 Host，
不包含用户绝对路径。Host 或项目 venv 未运行时 hook 会安静退出，不阻塞 Codex。

验证链路：

```zsh
scripts/saki-service.zsh status
scripts/saki-service.zsh logs
host/.venv/bin/saki-host doctor
```

运行 `doctor` 前应先用 `scripts/saki-service.zsh stop` 释放独占串口，检查结束后再 `start`。

## 6. 日常管理

```zsh
scripts/saki-service.zsh start
scripts/saki-service.zsh stop
scripts/saki-service.zsh restart
scripts/saki-service.zsh status
scripts/saki-service.zsh logs
scripts/saki-service.zsh uninstall
```

`uninstall` 会停止服务并删除 `~/Library/LaunchAgents/com.saki.agent-display.plist`，但保留
`~/Library/Logs/Saki` 以便排障。要立即停止 Agent 状态采集和运行日志，使用 `stop`；0.2.0
没有记录完整 hook、提示词或协议 JSON 的内容日志开关。

## 7. 隐私和日志

Host 只发送任务标题、归一化状态、高层动作摘要、进度、耗时、Agent 名称和模型名称。
它不发送隐藏思维链或完整工具输出，并在发送前执行以下处理：

- API Key、访问令牌、密码、Cookie/Authorization 等常见凭据替换为 `[REDACTED]`；
- URL 查询参数被移除；
- 用户主目录缩写为 `~`；
- 文本按协议上限截断，固件再次验证 UTF-8 和字段长度。

LaunchAgent 的日志目录权限为 `0700`，默认日志只包含连接状态、状态名、序号、重试次数和
设备诊断计数，不包含完整状态 JSON。USB 第一版以能物理接触 Mac 和设备为信任边界，不
提供应用层加密。

## 8. 常见问题

| 现象 | 处理方式 |
| --- | --- |
| `doctor` 提示端口被占用 | 先运行 `scripts/saki-service.zsh stop`，检查后再启动服务 |
| 只看到 `USB JTAG/serial debug unit` | 设备仍在 ROM 模式；不按 `K0`，短按一次 `RST` |
| 找不到 Saki 串口 | 检查数据线，短按 `RST`，再运行 `saki-host serial list` |
| 屏幕显示离线 | 检查 LaunchAgent 状态和日志；15 秒无有效消息时固件会主动离线 |
| 长时间停在工作/思考 | 120 秒没有 hook 时 Host 会降级为“等待用户”；检查 Codex hooks 是否被信任 |
| 达到 Agent 使用上限 | 若 Codex 产生 `Stop` 事件，Host 显示额度提示；没有终止事件时由 120 秒兜底处理 |
| 中文显示方框 | GB2312 常用简体中文已内置，范围外字符会使用替代字形 |
| 标题显示 `&#x20;` | 更新并重启 Mac Host；0.2.0 会把 hook 中的空白实体恢复为普通空格 |
| 点击任务卡没有展开详情 | 暗屏第一次点击只负责唤醒；确认屏幕已亮且当前快照包含 detail，再点击任务/动作卡 |
| 移动项目后服务无法启动 | 在新路径重新创建/确认 venv，并重新运行 `scripts/saki-service.zsh install` |

## 9. 恢复原厂固件

原厂完整 Flash 备份不得放入 Saki 发布包。恢复前确认备份确实来自当前设备、大小与 Flash
容量一致，并保存其 SHA-256。进入 ROM 下载模式后执行：

```zsh
esptool.py --chip esp32s3 \
  --port /dev/cu.usbmodemXXXXXX \
  --baud 460800 \
  write_flash 0x0 /path/to/verified-factory-16mb.bin
```

该操作会覆盖 Saki 和设备上的现有数据。恢复后只短按 `RST` 启动；不要把包含设备数据的
原厂备份上传到公开 release。
