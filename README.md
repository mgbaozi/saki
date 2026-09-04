# Saki

Saki 是运行在正点原子 ATK-DNESP32S3B3 / ESP32S3 BOX3 上的 AI Agent 状态屏。Mac 伴随程序采集 Codex 等 Agent 的生命周期事件，归一化后通过 USB CDC 发送给设备；后续将支持 BLE 和 Wi-Fi fallback。

## 文档

- [文档索引与命名规则](./docs/README.md)
- [0.2.0 产品和协议规格](./docs/versions/0.2.0/SPEC.md)
- [0.2.0 工程实施设计](./docs/versions/0.2.0/IMPLEMENTATION.md)
- [0.2.0 实施任务清单](./docs/versions/0.2.0/TASKS.md)
- [0.2.0 用户安装与排障指南](./docs/versions/0.2.0/USER_GUIDE.md)
- [0.2.0 发布说明](./docs/releases/0.2.0.md)
- [后续路线图](./docs/ROADMAP.md)

## 目录

- `firmware/`：ESP-IDF 5.5.3 + LVGL 8.4.0 设备固件。
- `host/`：运行在 Mac 上的 Python 伴随程序。
- `protocol/`：两端共享的协议 Schema 和测试 fixture。
- `scripts/`：环境、构建、烧录和检查脚本。
- `docs/`：产品规格、工程设计、任务与发布文档。

## 当前状态

- 固件已接入有界 NDJSON、握手、完整状态、ACK、session/seq、clear、ping/pong、error 和 TinyUSB CDC；正常启动时枚举为 `Saki Agent Display`。
- 320×240 横屏布局已在目标设备确认，Mac demo 已真机连续驱动 idle、starting、working、waiting_approval 和 completed。
- Mac Host 已支持 Product/Serial 发现、握手确认、ACK 重发、心跳、断线重连、latest snapshot 合并和 Unix Domain Socket hook collector；已连接端口连续 200 ms 消失后立即进入 10 秒、100 ms 间隔的快速重连窗口，之后才恢复指数退避。
- 项目级 Codex hooks 覆盖九个生命周期事件；hook 进程只投递归一化、脱敏后的完整快照，不传递原始工具输出。
- Host 会把 hook 文本中的 `&#x20;`、`&#32;`、`&nbsp;` 等空白实体还原为普通空格；其他 HTML 实体保持原样。
- 固件已加入 Noto Sans CJK SC 的完整 GB2312 子集（7,445 字形）作为 Montserrat fallback，固定文案和常用简体中文任务标题不再显示方框。
- 设备当前运行 `0.2.0` release：它会在握手后监测 Host 活动；连续 15 秒没有有效 status/clear/ping 时清除会话、保留最后快照并标记离线，恢复后强制重新握手。活动状态的执行时间由设备每秒更新，新 Agent 事件会用 Host 基准重新校准，终态或断线时冻结；未知进度显示为设备端 ease-in-out 平滑往返的短条动画，不再伪装成固定百分比。
- 固件已实现触摸摘要/详情切换、15 秒自动返回和暗屏首次点击仅唤醒；活动、空闲和断线三档感知亮度由 LVGL 遮罩实现。目标板背光脚仅提供开关，因此三档显示不会降低背光功耗。
- Host 和设备均执行 UTF-8 安全处理；设备在 JSON 解析前验证整帧编码，文本截断不会切断多字节字符，非法编码会被拒绝且不会进入 UI。
- 固件在 `pong.diagnostics` 中报告有效/非法/超长帧、旧序列、UI 覆盖、TX 丢弃和心跳超时，计数达到 `uint32` 上限后饱和；`0.2.0-dev` 进一步通过可选的 `pong.runtime` 报告当前/历史最低 heap、内部 heap 和 app/UI/USB stack watermark，Host `doctor` 会检查安全余量。
- 已建立相互隔离的 dev/release 构建：日常开发使用固件 `0.2.0-dev` 和 Host `0.2.0.dev0`；首个正式 release 定为 `0.2.0`。最新 dev 镜像 1,017,680 bytes、分区余 50%，release 镜像 923,648 bytes、分区余 55%。
- 78 项 Host/协议/文档与仓库可移植性测试和 Ruff 检查通过；包含 runtime 与 UI policy 覆盖在内的 17 项 ESP-IDF Unity 用例已在目标设备得到 `0 Failures`。
- 本地 hook IPC 携带不含内容的单调时钟时间戳；release 真机 12 个 hook→设备 ACK 样本平均 114.6 ms、P95/最大 169.4 ms。
- macOS LaunchAgent 已支持登录自动启动、异常拉起、状态/日志查询、受控重启和完整卸载。
- 当前 `0.2.0` 只剩第一版功能验收、Host 版本转正和最终封包/tag。24 小时运行、Mac
  睡眠/唤醒和 LCD 光学延迟测量已暂缓，不再阻止发布；触摸与视觉
  亮度已完成真机确认，当前 `0.2.0-candidate` 固件包已重新生成并校验。

## 快速构建

```zsh
source scripts/env-idf.zsh
scripts/build-firmware.zsh
# 等价于：scripts/build-firmware-profile.zsh dev

# 仅在准备正式发布候选时构建：
scripts/build-firmware-profile.zsh release

# 生成带校验和、构建信息和烧录说明的候选包：
scripts/package-release.zsh
```

Host 开发环境和离线检查：

```zsh
python3.12 -m venv host/.venv
host/.venv/bin/pip install -e 'host[dev]'
scripts/check.zsh
host/.venv/bin/saki-host serial list
host/.venv/bin/saki-host doctor
```

可选的 24 小时/10,000 次状态更新扩展验证使用专用 soak 命令；它会记录 ACK 延迟、设备
诊断、heap/stack 和 Host RSS，并拒绝覆盖已有报告。该测试不属于当前版本发布门槛：

```zsh
scripts/saki-service.zsh stop
host/.venv/bin/saki-host serial soak \
  --count 10000 --duration 86400 --sample-interval 60 \
  --report artifacts/soak-24h.json
scripts/saki-service.zsh start
```

临时释放常驻服务占用的串口后，可以发送单个完整快照或回放仓库内的脱敏会话：

```zsh
scripts/saki-service.zsh stop
host/.venv/bin/saki-host send --state working --title "有线调试" \
  --kind test --summary "正在验证单条状态" --percent 42
host/.venv/bin/saki-host replay protocol/fixtures/v1/sessions/basic.ndjson
scripts/saki-service.zsh start
```

`replay` 拒绝读取 `protocol/fixtures/v1/sessions` 之外的文件，避免误把真实会话或私密
日志发送到屏幕。录制消息中的 session、id 和 seq 不会复用，而是经过模型校验后以新的
设备会话重新编码。

推荐用 LaunchAgent 安装常驻 Host（自动按 Product String 发现设备）：

```zsh
scripts/saki-service.zsh install
scripts/saki-service.zsh status
scripts/saki-service.zsh logs
```

服务在当前用户登录后自动启动，异常退出后由 `launchd` 重新拉起。常用维护命令：

```zsh
scripts/saki-service.zsh stop
scripts/saki-service.zsh start
scripts/saki-service.zsh restart
scripts/saki-service.zsh uninstall
```

卸载会停止服务并删除 `~/Library/LaunchAgents/com.saki.agent-display.plist`，但保留
`~/Library/Logs/Saki` 供排障。项目目录或 `host/.venv` 位置改变后需要重新执行
`install`。已安装 LaunchAgent 时不要再手工启动第二个 Host；临时开发或独占串口测试前
先 `stop`，完成后再 `start`。例如运行 3 次 DTR 打开/关闭与握手测试：

```zsh
scripts/saki-service.zsh stop
host/.venv/bin/saki-host serial cycle --count 3 --interval 0.1
scripts/saki-service.zsh start
```

发布前可运行确定性非法输入/恢复测试。它只读取仓库内脱敏 fixture，默认再生成 32 个
有界随机变体；测试会占用业务串口，因此同样要先暂停常驻服务：

```zsh
scripts/saki-service.zsh stop
host/.venv/bin/saki-host serial fuzz --count 32 --seed 20260903
host/.venv/bin/saki-host doctor
scripts/saki-service.zsh start
```

物理拔插测试保持 LaunchAgent 运行，由旁路监视器记录设备消失、重新枚举以及 Host 完成
握手的时间（`--serial` 是稳定的 USB Serial Number，不是动态 `/dev/cu.*` 路径）：

```zsh
host/.venv/bin/python scripts/monitor-usb-reconnect.py \
  --serial 0123456789ab --cycles 3
```

Codex 在活跃状态下连续 120 秒没有产生任何 hook 时，Host 会把显示降级为
`waiting_user` 并提示检查 Mac，避免额度耗尽、客户端异常等无终止事件的情况永久停在
`thinking`。可用 `--stale-after SECONDS` 调整，设为 `0` 可关闭。

项目 hook 位于 `.codex/hooks.json`，并通过 `scripts/saki-hook.zsh` 从当前 checkout 定位 Host，不写死用户目录。Codex 首次加载或 hook 内容改变后会要求确认信任；Host 未运行时 hook 会安静退出，不阻塞 Agent。

烧录时必须显式传入当前动态发现的设备端口：

```zsh
scripts/flash-firmware.zsh /dev/cu.usbmodemXXXXXX dev
# 发布候选必须显式选择 release：
scripts/flash-firmware.zsh /dev/cu.usbmodemXXXXXX release
```

不要永久记录或写死 `/dev/cu.usbmodem*` 路径。

业务 CDC 固件运行时不能用 esptool 自动切换到 ROM 下载器。再次烧录时按住 `K0`、短按 `RST`、松开 `K0`，等待 `USB JTAG/serial debug unit` 端口出现；烧录完成后在不按 `K0` 的情况下短按一次 `RST` 启动应用。

## 许可证与项目名称

Saki 的原创代码和文档使用 [Apache License 2.0](./LICENSE)，归属及第三方声明见
[NOTICE](./NOTICE)。第三方组件继续适用各自的许可证和版权声明。

可以依照许可证使用、修改和分发本项目及衍生作品，但衍生项目、产品、服务或发行版不得
继续以 `Saki` 命名，也不得暗示获得原项目或作者背书。为说明代码来源和保留归属信息，
仍可合理引用 “Saki”；分发时必须保留适用的 `LICENSE`、`NOTICE`、原项目和作者信息。
