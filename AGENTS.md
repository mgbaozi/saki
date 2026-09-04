# Saki Agent 开发指南

本文件适用于整个仓库。开始修改前先阅读根目录 `README.md`，再按任务阅读当前版本的
`docs/versions/<version>/SPEC.md`、`IMPLEMENTATION.md` 和 `TASKS.md`。尚未进入版本的需求以
`docs/ROADMAP.md` 为准，不要从路线图直接推断已经实现。

## 项目边界

Saki 是一个 ESP32-S3 AI Agent 状态屏：

- `firmware/` 运行在 ATK-DNESP32S3B3 / ESP32S3 BOX3 上，使用 ESP-IDF 5.5.3 和 LVGL
  8.4.0；目标硬件为 16 MiB Flash、8 MiB Octal PSRAM、320×240 横屏。
- `host/` 运行在 macOS 上，采集并脱敏 Codex lifecycle hook，把平台事件归一化为完整状态
  快照，负责设备发现、握手、ACK/重试、心跳和重连。
- `protocol/` 是 Host 与固件共享的 v1 NDJSON 协议契约，包含 JSON Schema 和脱敏 fixture。
- `scripts/` 是受支持的环境、构建、烧录、测试、封包和 LaunchAgent 管理入口。
- `docs/` 保存版本化规格、实施任务、用户指南、发布说明和后续路线图。

Mac Host 只发送语义状态，不感知具体角色素材。未来加入角色或主题时，应由设备端把
`idle`、`thinking`、`working`、`waiting_user`、`waiting_approval`、`completed`、`failed`
和 `cancelled` 等状态映射到视觉资源，不要为单个角色分叉协议。

## 不可破坏的约束

- 不修改仓库外厂家示例目录（当前布局为 `../../examples`）；需要的 BSP 变更只在
  `firmware/components/vendor_box3_bsp/` 的已导入副本中完成，并维护其来源说明。
- 不运行 `idf.py set-target`。板级配置由 `firmware/config/sdkconfig.vendor`、公共 defaults
  和 dev/release profile 有序生成。
- 不写死 `/dev/cu.usbmodem*`。Host 使用 USB Product/Serial 发现设备；命令行烧录端口必须
  在操作当次动态确认。
- 不提交构建目录、虚拟环境、固件包、串口捕获、soak JSON、截图或其他本地 validation
  产物。`artifacts/` 除 `.gitkeep` 外保持忽略。
- 不提交真实设备序列号、用户主目录、凭据、完整 Agent 对话、隐藏思维链、原始工具输出
  或未经人工检查的 hook payload。示例设备 ID 使用 `0123456789ab`。
- 原厂 16 MiB Flash 备份只允许保存在仓库外，不得复制进源码、构建包或 GitHub Release。
- `firmware/components/saki_ui/fonts/saki_font_cjk_16.c` 是生成文件，不要手工编辑。字体变化
  使用 `scripts/generate_cjk_font.py`，保留来源哈希与 OFL 许可证。

## 修改规则

- 协议字段或状态语义变化时，必须同时检查并更新：`protocol/schema/v1/`、fixtures、Host
  模型/编码、固件解析/模型、两端测试、SPEC 和兼容性说明。
- 固件解析所有外部数据时保持长度有界、UTF-8 安全，并遵守 session/seq 和完整快照语义；
  非法或旧消息不得覆盖当前 UI。
- hook 处理必须快速失败且不阻塞 Agent。Host 不可把原始 hook 内容直接写入日志或发送到
  设备，只能发送经过归一化与脱敏的高层摘要。
- 保持 USB transport 与协议/UI 解耦，为 BLE、Wi-Fi 和多传输仲裁保留接口，但不要在没有
  对应版本规格时提前加入无线行为。
- UI 修改以真机 320×240 横屏为准，检查中英文边界、触摸、断线覆盖层、状态计时和动画；
  不要仅凭桌面截图或编译成功宣称完成。
- 优先复用现有脚本和组件，不在文档中复制容易漂移的长命令。

## 开发环境与检查

项目要求 Python 3.12 和 ESP-IDF 5.5.3。默认 IDF 路径由仓库布局推导，也可通过
`SAKI_IDF_PATH` 覆盖。

```zsh
python3.12 -m venv host/.venv
host/.venv/bin/pip install -e 'host[dev]'
scripts/check.zsh
scripts/build-firmware-profile.zsh dev
scripts/build-firmware-profile.zsh release
scripts/build-firmware-tests.zsh
```

最小验证要求：

- 纯 Host、协议或文档修改：运行 `scripts/check.zsh`。
- 固件或共享协议修改：额外构建 dev、release 和 Unity 测试固件。
- 影响硬件行为的修改：在用户明确允许并已连接目标设备时完成相称的真机验证；记录简短
  可复核摘要，不提交原始日志。
- 24 小时/10,000 次 soak、Mac 睡眠/唤醒和 LCD 光学延迟是可选工程验证，不阻止当前
  release；不要把未执行的可选项描述为已通过。

## 真机操作

- 构建本身不需要占用设备。需要独占应用 CDC 的 `doctor`、`send`、`replay`、`cycle`、
  `fuzz` 或 soak 前，先用 `scripts/saki-service.zsh stop` 停止常驻 Host，完成后恢复
  `scripts/saki-service.zsh start`。
- 运行 LaunchAgent 热插拔测试时保持服务运行，不要同时启动第二个 Host。
- 进入 ROM 下载模式：按住 `K0`，短按 `RST`，等待约一秒后松开 `K0`；烧录完成后不按
  `K0` 短按 `RST` 启动应用。
- 只使用 `scripts/flash-firmware.zsh <dynamic-port> <dev|release>` 烧录已经构建的 profile。
- Unity 真机测试会临时覆盖显示固件；退出测试后必须重新刷回正常 dev 或 release 固件并
  恢复 Host 服务。
- 不擦除整片 Flash、不恢复原厂镜像、不修改分区表，除非用户明确要求并确认目标设备。

## 文档与发布

- 版本文档放在 `docs/versions/<semver>/`；发布说明放在 `docs/releases/<semver>.md`；未排期
  工作放在 `docs/ROADMAP.md`。
- 规格写行为契约，实施文档写架构与操作，任务文档写完成状态与验证摘要；不要把本地原始
  validation 文件提交到仓库。
- 日常固件报告 dev 版本；只有 release profile 才能报告正式版本。Host 与固件正式版本
  必须一致。
- `scripts/package-release.zsh` 生成 candidate；`--final` 要求已有干净 Git 提交，且 Host
  和固件版本与脚本目标版本一致。
- 未经用户明确要求，不创建 commit、tag、GitHub Release，不推送远端，也不改写 Git 历史。
- 发布包必须包含校验和、构建信息、烧录说明、依赖锁和第三方许可证，不得包含设备凭据或
  原厂 Flash 备份。
- 项目原创内容使用 Apache License 2.0；第三方内容沿用各自许可证和版权声明。分发和衍生
  项目必须保留根目录 `LICENSE`、`NOTICE` 及适用的第三方声明，不得以 `Saki` 作为衍生
  项目、产品、服务或发行版名称，也不得暗示原项目或作者背书；描述来源时可以引用该名称。
