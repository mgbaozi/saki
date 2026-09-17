# Saki 0.5.0 用户指南

> 本文适用于已发布的 0.5.0。首次安装和 release 固件烧录从根目录
> [Quick Start](../../../README.md#quick-start) 开始。

## 从 0.4.5 升级

0.5 保持现有 Codex / Claude Code hook、Mac Host 服务、USB 优先、身份密钥和已绑定 BLE
fallback 配置。先按 Quick Start 下载并刷入 0.5.0 release 固件，再在已有源码目录更新 Host：

```zsh
git fetch --tags
git checkout v0.5.0
host/.venv/bin/pip install -e host
host/.venv/bin/saki-host hooks check --source codex
host/.venv/bin/saki-host hooks check --source claude_code
scripts/saki-service.zsh restart
```

只使用其中一个 Agent 时可以省略另一项检查。正常升级不需要清理 Session、重新生成身份密钥
或重新安装 hook；`hooks check` 报告缺失时才运行对应的 `hooks install`。

0.5 的 adapter 注册表是项目内部扩展边界，不是面向用户的任意插件系统。发布初期仍只承诺
Codex 和 Claude Code；看到通用来源标签或 `generic-source` capability 不代表可以把未知
程序的 JSON 直接交给 Host。

## Host 与固件兼容

| 使用组合 | 用户可见行为 |
| --- | --- |
| 0.5 Host + 0.5 firmware | Codex/Claude 使用通用来源标签和新视觉 |
| 0.5 Host + 0.4.5 firmware | Codex/Claude 保持现有显示；未来来源只能降级显示为 Agent |
| 0.4.5 Host + 0.5 firmware | 固件兼容既有多 Session，不要求 Host 理解新 capability |

Host 与固件版本不同不应擦除配置。遇到未知来源时，兼容模式宁可显示通用 `Agent`，也不能
发送旧固件无法解析的整组消息。

## 通用来源显示

0.5 设备使用 Host 提供的有界 `agent.name`，而不是在固件里只认 Codex / Claude Code。
侧栏会按可用宽度统一省略长名称，并显示状态和任务标题短句，不再显示难以辨认的四位假名
ID；来源名称只用于识别，不参与 Session 身份。Session 仍以本机 HMAC 假名隔离，相同原生 ID
来自不同 Agent 时不会合并。

主屏、最多三项侧栏、最近会话焦点、等待优先级、独立计时和完成保留时间沿用 0.4.5。
新增 Agent 种类不会增加 Host 32 项或设备 4 项上限。

底栏不再固定显示容易误解为错误数的 `!0`。没有隐藏待处理任务时只显示会话数量，例如
`最近会话 2/2`；只有存在投影外的等待批准、等待输入或失败任务时，才追加明确的
`待处理 N`。容量告警与待处理数分别显示。

运行态连续 2 分钟没有 hook 会标记为过期；再过 15 分钟仍无事件会从最近会话中隐藏。Host
仍保留有界记录，之后若收到迟到事件会重新显示。失败记录保留 10 分钟后自动移除；等待输入
和等待批准不会被自动隐藏。

若 Host 没有观察到本轮 `UserPromptSubmit`（例如安装 hook 前已经存在、仅恢复或压缩后的会话），
标题显示 `未捕获任务描述`，不会显示假名 ID 尾号，也不会读取 transcript 或工具参数猜测内容。
下一次正常提交 prompt 后会自动替换为经过本地过滤的短目标。

## 图片与趣味文案

图片用于强化状态辨识，文字状态、真实摘要、进度和等待操作提示仍是权威信息。资源不可用或
设备内存不足时自动回退文字界面，不需要联网下载。透明角色图后有随状态变化的深色底板和
亮色细边框，例如思考为紫色、完成为绿色、失败为红色；颜色只作辅助，不替代状态文字。

0.5 内置两套可切换素材包：

- `Saki Stage`（默认）：原创冰蓝发钢琴家/指挥主题，九种状态各有独立图标和状态文案；
- `经典`：不显示角色图与趣味文案，回退为 0.4.5 风格的状态圆点和核心文字。

文案模式提供：

- `关闭`：只显示核心状态与摘要；
- `轻量`：在不影响关键信息的位置显示一条稳定短文案。

默认使用 `Saki Stage` + `轻量`。同一状态不会随计时刷新不断更换文案。等待批准、等待输入、
失败时，真实操作信息保持第一行；详情、断线和 BLE 配对页面不显示趣味文案。

- 短按 K1：在 `轻量` 与 `关闭` 间切换文案；
- 按住 K1 2–5 秒后松开：切换到下一套素材包；
- K1 按住超过 5 秒：不执行切换，松开后可重新操作。

选择保存在设备 NVS 中，重启后继续生效。K2 仍只负责 BLE：短按重连、按住 2 秒配对、按住
5 秒清除绑定；素材包操作不会改变这些手势。

## 隐私与安全

0.5 不扩大数据采集范围。Adapter 只提取允许的身份、事件、工具类别和短目标；完整 prompt、
原始 hook、工具参数/结果、transcript 和模型回复不进入检查点或设备。短目标仍可能包含用户
请求原文片段，规则过滤不是完整匿名化，使用敏感任务时应评估屏幕可见内容。

通用 adapter 注册表是随 Saki 安装的显式代码，不扫描用户目录或加载远程 adapter。未知来源
和非法 source key 会被拒绝，不根据工具名或 payload 猜测产品。

## 故障排查与回退

状态没有更新时依次检查：

- `scripts/saki-service.zsh status` 确认 Host 正在运行；
- `saki-host hooks check --source <codex|claude_code>` 确认 wrapper、身份和配置完整；
- `saki-host sessions list` 只查看脱敏检查点，过期显示项可用 `sessions forget <id>` 清理；
- 独占 USB 的 `doctor` 前先停止服务，完成后重新启动，避免两个 Host 同时占用串口。

需要回退时重新下载并烧录 v0.4.5 release 包，然后将源码切到 `v0.4.5` 并重新安装 Host。
回退不会自动删除 0.5 的私有检查点或身份密钥；若旧 Host 无法解释新记录，可使用当前 0.5
CLI 的 `sessions forget all` 先清理显示记录。不要手工修改 protocol source 伪造第三方 Agent。
